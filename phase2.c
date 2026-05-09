#include <errno.h>
#include <limits.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#include "common.h"

#define QUEUE_INITIAL_CAPACITY 1024
#define MAX_THREADS 1024

typedef struct {
    numeric *items;
    size_t size;
    size_t capacity;
    pthread_mutex_t lock;
} WorkQueue;

typedef struct ConcurrentBitsetPage {
    numeric page_id;
    _Atomic uint64_t words[BITSET_PAGE_WORDS];
    struct ConcurrentBitsetPage *next;
} ConcurrentBitsetPage;

typedef struct {
    pthread_mutex_t lock;
    ConcurrentBitsetPage *pages;
} SparseBucket;

typedef struct {
    bool dense;
    _Atomic uint64_t *dense_words;
    size_t dense_word_count;
    SparseBucket *sparse_buckets;
} ConcurrentVisitedSet;

typedef struct {
    int id;
} WorkerContext;

typedef struct {
    int thread_count;
    bool count_only;
} AppConfig;

static ConcurrentVisitedSet visited;
static WorkQueue *queues = NULL;
static int queue_count = 0;
static bool count_only = false;

static pthread_mutex_t work_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t work_condition = PTHREAD_COND_INITIALIZER;

static atomic_size_t outstanding_work = 0;
static _Atomic uintmax_t emitted_count = 0;
static atomic_bool running = true;

static void die_pthread(int error, const char *message) {
    errno = error;
    die_errno(message);
}

static void lock_mutex(pthread_mutex_t *mutex, const char *message) {
    const int error = pthread_mutex_lock(mutex);
    if (error != 0) {
        die_pthread(error, message);
    }
}

static void unlock_mutex(pthread_mutex_t *mutex, const char *message) {
    const int error = pthread_mutex_unlock(mutex);
    if (error != 0) {
        die_pthread(error, message);
    }
}

static void concurrent_visited_init(ConcurrentVisitedSet *set) {
    set->dense = NUMERIC_BITS <= DENSE_BITSET_MAX_BITS;
    set->dense_words = NULL;
    set->dense_word_count = 0;
    set->sparse_buckets = NULL;

    if (set->dense) {
        const uint128_t odd_values = ((uint128_t)1) << (NUMERIC_BITS - 1);
        set->dense_word_count = (size_t)((odd_values + BITSET_WORD_BITS - 1) / BITSET_WORD_BITS);
        set->dense_words = checked_calloc_array(set->dense_word_count, sizeof(*set->dense_words));
        return;
    }

    set->sparse_buckets = checked_calloc_array(SPARSE_BUCKET_COUNT, sizeof(*set->sparse_buckets));
    for (size_t i = 0; i < SPARSE_BUCKET_COUNT; i++) {
        const int error = pthread_mutex_init(&set->sparse_buckets[i].lock, NULL);
        if (error != 0) {
            die_pthread(error, "pthread_mutex_init");
        }
    }
}

static void concurrent_visited_destroy(ConcurrentVisitedSet *set) {
    free(set->dense_words);

    if (set->sparse_buckets != NULL) {
        for (size_t i = 0; i < SPARSE_BUCKET_COUNT; i++) {
            ConcurrentBitsetPage *page = set->sparse_buckets[i].pages;

            while (page != NULL) {
                ConcurrentBitsetPage *next = page->next;
                free(page);
                page = next;
            }

            const int error = pthread_mutex_destroy(&set->sparse_buckets[i].lock);
            if (error != 0) {
                die_pthread(error, "pthread_mutex_destroy");
            }
        }

        free(set->sparse_buckets);
    }

    set->dense_words = NULL;
    set->dense_word_count = 0;
    set->sparse_buckets = NULL;
}

static bool concurrent_visited_check_and_add(ConcurrentVisitedSet *set, numeric value) {
    const numeric odd_index = (numeric)(value >> 1);

    if (set->dense) {
        const size_t bit_index = (size_t)odd_index;
        const size_t word_index = bit_index / BITSET_WORD_BITS;
        const uint64_t mask = UINT64_C(1) << (bit_index % BITSET_WORD_BITS);
        const uint64_t previous = atomic_fetch_or(&set->dense_words[word_index], mask);

        return (previous & mask) != 0;
    }

    const numeric page_id = (numeric)(odd_index >> BITSET_PAGE_BITS);
    const unsigned page_offset = (unsigned)(odd_index & (numeric)(BITSET_PAGE_VALUES - 1));
    const size_t bucket_index = (size_t)(hash_numeric(page_id) % SPARSE_BUCKET_COUNT);
    SparseBucket *bucket = &set->sparse_buckets[bucket_index];

    lock_mutex(&bucket->lock, "pthread_mutex_lock");
    ConcurrentBitsetPage *page = bucket->pages;

    while (page != NULL && page->page_id != page_id) {
        page = page->next;
    }

    if (page == NULL) {
        page = checked_calloc_array(1, sizeof(*page));
        page->page_id = page_id;
        page->next = bucket->pages;
        bucket->pages = page;
    }

    unlock_mutex(&bucket->lock, "pthread_mutex_unlock");

    const size_t word_index = page_offset / BITSET_WORD_BITS;
    const uint64_t mask = UINT64_C(1) << (page_offset % BITSET_WORD_BITS);
    const uint64_t previous = atomic_fetch_or(&page->words[word_index], mask);

    return (previous & mask) != 0;
}

static bool check_exists_and_add(numeric v) {
    return concurrent_visited_check_and_add(&visited, v);
}

static void emit_value(numeric value) {
    atomic_fetch_add(&emitted_count, 1);

    if (!count_only) {
        PRINT_U(value);
    }
}

static void queue_init(WorkQueue *queue) {
    queue->capacity = QUEUE_INITIAL_CAPACITY;
    queue->size = 0;
    queue->items = checked_malloc_array(queue->capacity, sizeof(*queue->items));

    const int error = pthread_mutex_init(&queue->lock, NULL);
    if (error != 0) {
        die_pthread(error, "pthread_mutex_init");
    }
}

static void queue_destroy(WorkQueue *queue) {
    free(queue->items);

    const int error = pthread_mutex_destroy(&queue->lock);
    if (error != 0) {
        die_pthread(error, "pthread_mutex_destroy");
    }
}

static void queue_push(WorkQueue *queue, numeric value) {
    lock_mutex(&queue->lock, "pthread_mutex_lock");

    if (queue->size == queue->capacity) {
        grow_numeric_array(&queue->items, &queue->capacity);
    }

    queue->items[queue->size++] = value;
    unlock_mutex(&queue->lock, "pthread_mutex_unlock");
}

static bool queue_pop(WorkQueue *queue, numeric *value) {
    bool found = false;

    lock_mutex(&queue->lock, "pthread_mutex_lock");
    if (queue->size > 0) {
        *value = queue->items[--queue->size];
        found = true;
    }
    unlock_mutex(&queue->lock, "pthread_mutex_unlock");

    return found;
}

static void signal_work_available(void) {
    lock_mutex(&work_mutex, "pthread_mutex_lock");
    const int error = pthread_cond_signal(&work_condition);
    if (error != 0) {
        die_pthread(error, "pthread_cond_signal");
    }
    unlock_mutex(&work_mutex, "pthread_mutex_unlock");
}

static void push_work(int queue_id, numeric value) {
    atomic_fetch_add(&outstanding_work, 1);
    queue_push(&queues[queue_id], value);
    signal_work_available();
}

static void add_next(int worker_id, numeric value) {
    if (!check_exists_and_add(value)) {
        emit_value(value);
        push_work(worker_id, value);
    }
}

static void process_value(int worker_id, numeric value) {
    numeric next;
    if (mul2_add1(value, &next)) {
        add_next(worker_id, next);
    }

    numeric divided;
    if (divide_exact(value, 3, &divided)) {
        add_next(worker_id, divided);
    }
}

static bool try_get_work(int worker_id, numeric *item) {
    if (queue_pop(&queues[worker_id], item)) {
        return true;
    }

    for (int offset = 1; offset < queue_count; offset++) {
        const int victim_id = (worker_id + offset) % queue_count;

        if (queue_pop(&queues[victim_id], item)) {
            return true;
        }
    }

    return false;
}

static bool wait_for_work(int worker_id, numeric *item) {
    if (try_get_work(worker_id, item)) {
        return true;
    }

    lock_mutex(&work_mutex, "pthread_mutex_lock");
    while (atomic_load(&running)) {
        if (try_get_work(worker_id, item)) {
            unlock_mutex(&work_mutex, "pthread_mutex_unlock");
            return true;
        }

        const int error = pthread_cond_wait(&work_condition, &work_mutex);
        if (error != 0) {
            die_pthread(error, "pthread_cond_wait");
        }
    }
    unlock_mutex(&work_mutex, "pthread_mutex_unlock");

    return false;
}

static void finish_work_item(void) {
    if (atomic_fetch_sub(&outstanding_work, 1) == 1) {
        atomic_store(&running, false);

        lock_mutex(&work_mutex, "pthread_mutex_lock");
        const int error = pthread_cond_broadcast(&work_condition);
        if (error != 0) {
            die_pthread(error, "pthread_cond_broadcast");
        }
        unlock_mutex(&work_mutex, "pthread_mutex_unlock");
    }
}

static void *worker(void *arg) {
    const WorkerContext *context = arg;
    const int worker_id = context->id;
    numeric item;

    while (wait_for_work(worker_id, &item)) {
        process_value(worker_id, item);
        finish_work_item();
    }

    return NULL;
}

static int default_thread_count(void) {
    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online <= 0) {
        return 1;
    }

    return online > MAX_THREADS ? MAX_THREADS : (int)online;
}

static int parse_thread_count(const char *arg) {
    char *end = NULL;
    errno = 0;
    const long requested = strtol(arg, &end, 10);

    if (errno != 0 || end == arg || *end != '\0' || requested <= 0 || requested > MAX_THREADS) {
        die_message("thread count must be an integer in the range 1..1024");
    }

    return (int)requested;
}

static AppConfig parse_args(int argc, char *argv[]) {
    AppConfig config = {
        .thread_count = default_thread_count(),
        .count_only = false,
    };
    bool has_thread_count = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--count") == 0) {
            config.count_only = true;
            continue;
        }

        if (has_thread_count) {
            die_message("usage: phase2 [thread-count] [--count]");
        }

        config.thread_count = parse_thread_count(argv[i]);
        has_thread_count = true;
    }

    return config;
}

static void stop_workers(void) {
    atomic_store(&running, false);

    lock_mutex(&work_mutex, "pthread_mutex_lock");
    const int error = pthread_cond_broadcast(&work_condition);
    if (error != 0) {
        die_pthread(error, "pthread_cond_broadcast");
    }
    unlock_mutex(&work_mutex, "pthread_mutex_unlock");
}

int main(int argc, char *argv[]) {
    const AppConfig config = parse_args(argc, argv);
    count_only = config.count_only;

    configure_stdout();
    concurrent_visited_init(&visited);

    const int max_threads = config.thread_count;
    queue_count = max_threads;
    queues = checked_malloc_array((size_t)queue_count, sizeof(*queues));
    for (int i = 0; i < queue_count; i++) {
        queue_init(&queues[i]);
    }

    pthread_t *threads = checked_malloc_array((size_t)max_threads, sizeof(*threads));
    WorkerContext *contexts = checked_malloc_array((size_t)max_threads, sizeof(*contexts));
    int started_threads = 0;

    const numeric seed = 1;
    if (!check_exists_and_add(seed)) {
        emit_value(seed);
        push_work(0, seed);
    }

    for (int i = 0; i < max_threads; i++) {
        contexts[i].id = i;
        const int error = pthread_create(&threads[i], NULL, worker, &contexts[i]);
        if (error != 0) {
            stop_workers();
            for (int j = 0; j < started_threads; j++) {
                const int join_error = pthread_join(threads[j], NULL);
                if (join_error != 0) {
                    die_pthread(join_error, "pthread_join");
                }
            }
            die_pthread(error, "pthread_create");
        }
        started_threads++;
    }

    for (int i = 0; i < started_threads; i++) {
        const int error = pthread_join(threads[i], NULL);
        if (error != 0) {
            die_pthread(error, "pthread_join");
        }
    }

    for (int i = 0; i < queue_count; i++) {
        queue_destroy(&queues[i]);
    }
    concurrent_visited_destroy(&visited);
    free(queues);
    free(contexts);
    free(threads);

    if (count_only) {
        print_count(atomic_load(&emitted_count));
    }

    return 0;
}
