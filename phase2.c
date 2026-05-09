#include <errno.h>
#include <limits.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#include "common.h"

#define INITIAL_CAPACITY 0xA00000
#define HASH_SIZE 8192
#define MAX_THREADS 1024

typedef struct {
    numeric *array;
    size_t size;
    size_t capacity;
    pthread_rwlock_t lock;
} HashBucket;

static HashBucket *hash_table = NULL;

static numeric *stack = NULL;
static size_t stack_size = 0;
static size_t stack_capacity = 0;

static pthread_mutex_t stack_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t stack_condition = PTHREAD_COND_INITIALIZER;

static atomic_int active_workers = 0;
static atomic_bool running = true;

static void die_pthread(int error, const char *message) {
    errno = error;
    die_errno(message);
}

static inline size_t hash(numeric key) {
    return (size_t)((key >> 1) % HASH_SIZE);
}

static void init_hash_table(void) {
    hash_table = checked_calloc_array(HASH_SIZE, sizeof(*hash_table));

    for (size_t i = 0; i < HASH_SIZE; i++) {
        hash_table[i].capacity = INITIAL_CAPACITY / HASH_SIZE;
        hash_table[i].array = checked_malloc_array(hash_table[i].capacity, sizeof(*hash_table[i].array));

        const int error = pthread_rwlock_init(&hash_table[i].lock, NULL);
        if (error != 0) {
            die_pthread(error, "pthread_rwlock_init");
        }
    }
}

static bool check_exists_and_add(numeric v) {
    const size_t bucket_idx = hash(v);
    HashBucket *bucket = &hash_table[bucket_idx];

    int error = pthread_rwlock_rdlock(&bucket->lock);
    if (error != 0) {
        die_pthread(error, "pthread_rwlock_rdlock");
    }
    size_t pos = binary_search_insert_position(bucket->array, bucket->size, v);
    const bool exists = pos < bucket->size && bucket->array[pos] == v;
    error = pthread_rwlock_unlock(&bucket->lock);
    if (error != 0) {
        die_pthread(error, "pthread_rwlock_unlock");
    }

    if (exists) {
        return true;
    }

    error = pthread_rwlock_wrlock(&bucket->lock);
    if (error != 0) {
        die_pthread(error, "pthread_rwlock_wrlock");
    }
    pos = binary_search_insert_position(bucket->array, bucket->size, v);
    if (pos < bucket->size && bucket->array[pos] == v) {
        error = pthread_rwlock_unlock(&bucket->lock);
        if (error != 0) {
            die_pthread(error, "pthread_rwlock_unlock");
        }
        return true;
    }

    if (bucket->size + 1 >= bucket->capacity) {
        grow_numeric_array(&bucket->array, &bucket->capacity);
    }

    memmove(&bucket->array[pos + 1], &bucket->array[pos],
            (bucket->size - pos) * sizeof(numeric));
    bucket->array[pos] = v;
    bucket->size++;

    error = pthread_rwlock_unlock(&bucket->lock);
    if (error != 0) {
        die_pthread(error, "pthread_rwlock_unlock");
    }
    return false;
}

static void push_stack(numeric value) {
    int error = pthread_mutex_lock(&stack_mutex);
    if (error != 0) {
        die_pthread(error, "pthread_mutex_lock");
    }
    if (stack_size == stack_capacity) {
        grow_numeric_array(&stack, &stack_capacity);
    }
    stack[stack_size++] = value;
    error = pthread_cond_signal(&stack_condition);
    if (error != 0) {
        die_pthread(error, "pthread_cond_signal");
    }
    error = pthread_mutex_unlock(&stack_mutex);
    if (error != 0) {
        die_pthread(error, "pthread_mutex_unlock");
    }
}

static void add_next(numeric value) {
    if (!check_exists_and_add(value)) {
        PRINT_U(value);
        push_stack(value);
    }
}

static void process_value(numeric value) {
    numeric next;
    if (mul2_add1(value, &next)) {
        add_next(next);
    }

    numeric divided;
    if (divide_exact(value, 3, &divided)) {
        add_next(divided);
    }
}

static void *worker(void *arg) {
    (void)arg;
    while (atomic_load(&running)) {
        int error = pthread_mutex_lock(&stack_mutex);
        if (error != 0) {
            die_pthread(error, "pthread_mutex_lock");
        }
        while (stack_size == 0 && atomic_load(&running)) {
            if (atomic_load(&active_workers) == 0) {
                atomic_store(&running, false);
                error = pthread_cond_broadcast(&stack_condition);
                if (error != 0) {
                    die_pthread(error, "pthread_cond_broadcast");
                }
                error = pthread_mutex_unlock(&stack_mutex);
                if (error != 0) {
                    die_pthread(error, "pthread_mutex_unlock");
                }
                return NULL;
            }
            error = pthread_cond_wait(&stack_condition, &stack_mutex);
            if (error != 0) {
                die_pthread(error, "pthread_cond_wait");
            }
        }

        if (!atomic_load(&running)) {
            error = pthread_mutex_unlock(&stack_mutex);
            if (error != 0) {
                die_pthread(error, "pthread_mutex_unlock");
            }
            break;
        }

        const numeric item = stack[--stack_size];
        atomic_fetch_add(&active_workers, 1);
        error = pthread_mutex_unlock(&stack_mutex);
        if (error != 0) {
            die_pthread(error, "pthread_mutex_unlock");
        }

        process_value(item);

        atomic_fetch_sub(&active_workers, 1);

        error = pthread_mutex_lock(&stack_mutex);
        if (error != 0) {
            die_pthread(error, "pthread_mutex_lock");
        }
        if (stack_size == 0 && atomic_load(&active_workers) == 0) {
            atomic_store(&running, false);
            error = pthread_cond_broadcast(&stack_condition);
            if (error != 0) {
                die_pthread(error, "pthread_cond_broadcast");
            }
        }
        error = pthread_mutex_unlock(&stack_mutex);
        if (error != 0) {
            die_pthread(error, "pthread_mutex_unlock");
        }
    }
    return NULL;
}

static int get_num_threads_from_args(int argc, char *argv[]) {
    if (argc > 1) {
        char *end = NULL;
        errno = 0;
        const long requested = strtol(argv[1], &end, 10);

        if (errno != 0 || end == argv[1] || *end != '\0' || requested <= 0 || requested > MAX_THREADS) {
            die_message("thread count must be an integer in the range 1..1024");
        }

        return (int)requested;
    }

    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    if (online <= 0) {
        return 1;
    }

    return online > MAX_THREADS ? MAX_THREADS : (int)online;
}

static void stop_workers(void) {
    int error = pthread_mutex_lock(&stack_mutex);
    if (error != 0) {
        die_pthread(error, "pthread_mutex_lock");
    }

    atomic_store(&running, false);
    error = pthread_cond_broadcast(&stack_condition);
    if (error != 0) {
        die_pthread(error, "pthread_cond_broadcast");
    }

    error = pthread_mutex_unlock(&stack_mutex);
    if (error != 0) {
        die_pthread(error, "pthread_mutex_unlock");
    }
}

int main(int argc, char *argv[]) {
    configure_stdout();
    init_hash_table();

    stack_capacity = INITIAL_CAPACITY;
    stack = checked_malloc_array(stack_capacity, sizeof(*stack));
    const numeric seed = 1;
    if (!check_exists_and_add(seed)) {
        PRINT_U(seed);
        push_stack(seed);
    }

    const int max_threads = get_num_threads_from_args(argc, argv);
    pthread_t *threads = checked_malloc_array((size_t)max_threads, sizeof(*threads));
    int started_threads = 0;

    for (int i = 0; i < max_threads; i++) {
        const int error = pthread_create(&threads[i], NULL, worker, NULL);
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

    for (size_t i = 0; i < HASH_SIZE; i++) {
        free(hash_table[i].array);
        pthread_rwlock_destroy(&hash_table[i].lock);
    }
    free(hash_table);
    free(stack);
    free(threads);

    return 0;
}