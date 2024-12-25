#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#define MASK64(v) (uint64_t) ((v) & 0xFFFFFFFFFFFFFFFF)
#define INITIAL_CAPACITY 1024

#if defined(__BITINT_MAXWIDTH__) && __BITINT_MAXWIDTH__ >= 128
typedef unsigned _BitInt(128) uint128_t;
#else
typedef unsigned __int128 uint128_t;
#endif

typedef uint16_t numeric;

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

    if (odds_size) {
        pos = binary_search_insert_position(odds, odds_size, v);

        if (pos < odds_size && odds[pos] == v) {
            return true;
        }
    }

    if (odds_size + 1 >= odds_capacity) {
        realloc_stack(&odds, &odds_capacity);
    }

    memmove(&odds[pos + 1], &odds[pos], (odds_size - pos) * sizeof(numeric));

    odds[pos] = v;
    odds_size++;

    return false;
}

static void push_stack(numeric value) {
    stack[stack_size++] = value;

    if (stack_size == stack_capacity) {
        realloc_stack(&stack, &stack_capacity);
    }
}

static inline numeric pop_stack(void) {
    return stack[--stack_size];
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

int main(void) {
    odds_capacity = INITIAL_CAPACITY;
    odds = malloc(odds_capacity * sizeof(numeric));

    stack_capacity = INITIAL_CAPACITY;
    stack = malloc(stack_capacity * sizeof(numeric));

    push_stack(1);
    while (stack_size > 0) {
        main_enumeration(pop_stack());
    }

    free(stack);
    free(odds);

    return 0;
}
