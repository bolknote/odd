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

typedef unsigned numeric;

#define MUL2_ADD1(x) ({ \
    typeof(x) y; \
    __builtin_mul_overflow(x, 2, &y) || __builtin_add_overflow(y, 1, &y) ? 0 : y; \
})

#define PRINT_U(x) ({ \
    _Generic((x), \
        uint128_t: printf_u128, \
        uint64_t: printf_u64, \
        uint32_t: printf_u32 \
    ); \
})(x)

static void printf_u128(const uint128_t v) {
    unsigned long long low, high;

    bool is_little_endian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;

    if (is_little_endian) {
        low  = MASK64(v);
        high = MASK64(v >> 64);
    } else {
        high = MASK64(v);
        low  = MASK64(v >> 64);
    }

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

static numeric* odds = NULL;
static size_t odds_size = 0;
static size_t odds_capacity = 0;

static numeric *stack = NULL;
static size_t stack_size = 0;
static size_t stack_capacity = 0;

static pthread_mutex_t stack_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t odds_rwlock = PTHREAD_RWLOCK_INITIALIZER;
static pthread_cond_t stack_condition = PTHREAD_COND_INITIALIZER;

static atomic_int threads_count = 0;
static atomic_bool running = true;

static numeric is_divided_by(numeric v, uint_fast8_t by) {
    const numeric result = v / by;
    return result * by == v ? result : 0;
}

static void realloc_stack(numeric **arr, size_t *capacity) {
    *capacity *= 2;

    *arr = realloc(*arr, *capacity * sizeof(numeric));

    if (*arr == NULL) {
        exit(1);
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

static bool check_exists_and_add(const numeric v) {
    size_t pos = 0;

    pthread_rwlock_rdlock(&odds_rwlock);

    if (odds_size) {
        pos = binary_search_insert_position(odds, odds_size, v);

        if (pos < odds_size && odds[pos] == v) {
            pthread_rwlock_unlock(&odds_rwlock);
            return true;
        }
    }

    pthread_rwlock_unlock(&odds_rwlock);
    pthread_rwlock_wrlock(&odds_rwlock);

    if (odds_size + 1 >= odds_capacity) {
        realloc_stack(&odds, &odds_capacity);
    }

    memmove(&odds[pos + 1], &odds[pos], (odds_size - pos) * sizeof(numeric));

    odds[pos] = v;
    odds_size++;

    pthread_rwlock_unlock(&odds_rwlock);
    return false;
}

static void push_stack(numeric value) {
    pthread_mutex_lock(&stack_mutex);

    stack[stack_size++] = value;

    if (stack_size == stack_capacity) {
        realloc_stack(&stack, &stack_capacity);
    }

    pthread_cond_signal(&stack_condition);

    pthread_mutex_unlock(&stack_mutex);
}

static inline numeric pop_stack(void) {
    pthread_mutex_lock(&stack_mutex);

    while (stack_size == 0) {
        pthread_cond_wait(&stack_condition, &stack_mutex);
    }

    numeric value = stack[--stack_size];

    pthread_mutex_unlock(&stack_mutex);
    return value;
}

static void main_enumeration(numeric counter) {
    do {
        if (check_exists_and_add(counter)) {
            return;
        }

        PRINT_U(counter);

        numeric cnt_by_3 = is_divided_by(counter, 3);

        if (cnt_by_3) {
            do {
                if (check_exists_and_add(cnt_by_3)) {
                    break;
                }

                PRINT_U(cnt_by_3);
                cnt_by_3 = is_divided_by(cnt_by_3, 3);

                if (cnt_by_3) {
                    numeric next = MUL2_ADD1(cnt_by_3);
                    if (next > 0) {
                        push_stack(next);
                    }
                }
            } while (cnt_by_3);
        }

        counter = MUL2_ADD1(counter);
    } while (counter);
}

void* worker() {
    while (atomic_load(&running)) {
        if (stack_size > 0) {
            numeric item = pop_stack();
            main_enumeration(item);
        } else {
            pthread_mutex_lock(&stack_mutex);
            pthread_cond_wait(&stack_condition, &stack_mutex);
            pthread_mutex_unlock(&stack_mutex);
        }
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
    odds_capacity = INITIAL_CAPACITY;
    odds = malloc(odds_capacity * sizeof(numeric));

    stack_capacity = INITIAL_CAPACITY;
    stack = malloc(stack_capacity * sizeof(numeric));

    push_stack(1);

    const int max_threads = get_num_threads_from_args(argc, argv);
    pthread_t threads[max_threads];

    for (int i = 0; i < max_threads; i++) {
        pthread_create(&threads[i], NULL, worker, NULL);
        atomic_fetch_add(&threads_count, 1);
    }

    for (int i = 0; i < max_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    atomic_store(&running, false);

    free(stack);
    free(odds);

    return 0;
}
