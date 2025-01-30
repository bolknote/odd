#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <sys/sysctl.h>

#define MASK64(v) (uint64_t) ((v) & 0xFFFFFFFFFFFFFFFF)
#define INITIAL_CAPACITY 0xA00000

#if defined(__BITINT_MAXWIDTH__) && __BITINT_MAXWIDTH__ >= 128
typedef unsigned _BitInt(128) uint128_t;
#else
typedef unsigned __int128 uint128_t;
#endif

typedef uint32_t numeric;

#define MUL2_ADD1(x) ({ \
    typeof(x) y; \
    __builtin_mul_overflow(x, 2, &y) || __builtin_add_overflow(y, 1, &y) ? 0 : y; \
})

#define PRINT_U(x) ({ \
    _Generic((x), \
        uint128_t: printf_u128, \
        uint64_t: printf_u64, \
        uint32_t: printf_u32, \
        uint16_t: printf_u32 \
    ); \
})(x)

static void printf_u128(const uint128_t v) {
    unsigned long long low, high;

    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        low  = MASK64(v);
        high = MASK64(v >> 64);
    #else
        high = MASK64(v);
        low  = MASK64(v >> 64);
    #endif

    printf("0x%016llX%016llX\n", high, low);
    fflush(stdout);
}

static inline void printf_u64(const unsigned long long v) {
    printf("0x%016llX\n", v);
    fflush(stdout);
}

static inline void printf_u32(const unsigned int v) {
    printf("0x%08X\n", v);
    fflush(stdout);
}

#define HASH_SIZE 8192

typedef struct {
    numeric* array;
    size_t size;
    size_t capacity;
    pthread_rwlock_t lock;
} HashBucket;

static HashBucket* hash_table = NULL;

static numeric *stack = NULL;
static size_t stack_size = 0;
static size_t stack_capacity = 0;

static pthread_mutex_t stack_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t stack_condition = PTHREAD_COND_INITIALIZER;

static atomic_int active_workers = 0;
static atomic_bool running = true;

static numeric is_divided_by(numeric v, uint_fast8_t by) {
    const numeric result = v / by;
    return result * by == v ? result : 0;
}

static void realloc_stack(numeric **arr, size_t *capacity) {
    *capacity *= 2;
    *arr = realloc(*arr, *capacity * sizeof(numeric));
    if (*arr == NULL) exit(1);
}

static inline size_t hash(numeric key) {
    return key % HASH_SIZE;
}

static void init_hash_table() {
    hash_table = calloc(HASH_SIZE, sizeof(HashBucket));
    for (size_t i = 0; i < HASH_SIZE; i++) {
        hash_table[i].capacity = INITIAL_CAPACITY / HASH_SIZE;
        hash_table[i].array = malloc(hash_table[i].capacity * sizeof(numeric));
        pthread_rwlock_init(&hash_table[i].lock, NULL);
    }
}

static size_t binary_search_insert_position(const numeric *arr, size_t size, numeric v) {
    size_t left = 0, right = size;
    while (left < right) {
        size_t mid = left + (right - left) / 2;

        if (arr[mid] < v) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }
    return left;
}

static bool check_exists_and_add(numeric v) {
    size_t bucket_idx = hash(v);
    HashBucket* bucket = &hash_table[bucket_idx];

    pthread_rwlock_rdlock(&bucket->lock);
    size_t pos = binary_search_insert_position(bucket->array, bucket->size, v);
    bool exists = pos < bucket->size && bucket->array[pos] == v;
    pthread_rwlock_unlock(&bucket->lock);

    if (exists) return true;

    pthread_rwlock_wrlock(&bucket->lock);
    pos = binary_search_insert_position(bucket->array, bucket->size, v);
    if (pos < bucket->size && bucket->array[pos] == v) {
        pthread_rwlock_unlock(&bucket->lock);
        return true;
    }

    if (bucket->size + 1 >= bucket->capacity) {
        bucket->capacity *= 2;
        bucket->array = realloc(bucket->array, bucket->capacity * sizeof(numeric));
    }

    memmove(&bucket->array[pos + 1], &bucket->array[pos], 
            (bucket->size - pos) * sizeof(numeric));
    bucket->array[pos] = v;
    bucket->size++;
    pthread_rwlock_unlock(&bucket->lock);
    return false;
}

static void push_stack(numeric value) {
    pthread_mutex_lock(&stack_mutex);
    if (stack_size == stack_capacity) {
        realloc_stack(&stack, &stack_capacity);
    }
    stack[stack_size++] = value;
    pthread_cond_signal(&stack_condition);
    pthread_mutex_unlock(&stack_mutex);
}

static void main_enumeration(numeric counter) {
    do {
        if (check_exists_and_add(counter)) return;
        PRINT_U(counter);

        numeric cnt_by_3 = is_divided_by(counter, 3);
        if (cnt_by_3) {
            do {
                if (check_exists_and_add(cnt_by_3)) break;
                PRINT_U(cnt_by_3);
                cnt_by_3 = is_divided_by(cnt_by_3, 3);
                if (cnt_by_3) {
                    numeric next = MUL2_ADD1(cnt_by_3);
                    if (next) push_stack(next);
                }
            } while (cnt_by_3);
        }
        counter = MUL2_ADD1(counter);
    } while (counter);
}

void* worker(void* arg) {
    (void)arg;
    while (atomic_load(&running)) {
        pthread_mutex_lock(&stack_mutex);
        while (stack_size == 0 && atomic_load(&running)) {
            if (atomic_load(&active_workers) == 0) {
                atomic_store(&running, false);
                pthread_cond_broadcast(&stack_condition);
                pthread_mutex_unlock(&stack_mutex);
                return NULL;
            }
            pthread_cond_wait(&stack_condition, &stack_mutex);
        }

        if (!atomic_load(&running)) {
            pthread_mutex_unlock(&stack_mutex);
            break;
        }

        numeric item = stack[--stack_size];
        atomic_fetch_add(&active_workers, 1);
        pthread_mutex_unlock(&stack_mutex);

        main_enumeration(item);

        atomic_fetch_sub(&active_workers, 1);

        pthread_mutex_lock(&stack_mutex);
        if (stack_size == 0 && atomic_load(&active_workers) == 0) {
            atomic_store(&running, false);
            pthread_cond_broadcast(&stack_condition);
        }
        pthread_mutex_unlock(&stack_mutex);
    }
    return NULL;
}

int get_num_threads_from_args(int argc, char* argv[]) {
    int num_cores;

    if (argc > 1) {
        num_cores = atoi(argv[1]);

        if (num_cores > 0) {
            return num_cores;
        }
    }

#if defined(__linux__)
    num_cores = get_nprocs();
#elif defined(__APPLE__)
    int mib[2] = { CTL_HW, HW_NCPU };
    size_t len = sizeof(num_cores);
    sysctl(mib, 2, &num_cores, &len, NULL, 0);
#else
    num_cores = 1;
#endif

    return num_cores;
}

int main(int argc, char* argv[]) {
    init_hash_table();

    stack_capacity = INITIAL_CAPACITY;
    stack = malloc(stack_capacity * sizeof(numeric));
    push_stack(1);

    const int max_threads = get_num_threads_from_args(argc, argv);
    pthread_t threads[max_threads];

    for (int i = 0; i < max_threads; i++) {
        pthread_create(&threads[i], NULL, worker, NULL);
    }

    for (int i = 0; i < max_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    for (size_t i = 0; i < HASH_SIZE; i++) {
        free(hash_table[i].array);
        pthread_rwlock_destroy(&hash_table[i].lock);
    }
    free(hash_table);
    free(stack);

    return 0;
}