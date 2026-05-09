#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#if defined(__SIZEOF_INT128__)
typedef unsigned __int128 uint128_t;
#elif defined(__BITINT_MAXWIDTH__) && __BITINT_MAXWIDTH__ >= 128
typedef unsigned _BitInt(128) uint128_t;
#else
#error "128-bit unsigned integer type is required"
#endif

#ifndef NUMERIC_TYPE
#define NUMERIC_TYPE uint32_t
#endif

typedef NUMERIC_TYPE numeric;

#if defined(__GNUC__) || defined(__clang__)
#define ODDS_UNUSED __attribute__((unused))
#else
#define ODDS_UNUSED
#endif

static ODDS_UNUSED void die_errno(const char *message) {
    perror(message);
    exit(EXIT_FAILURE);
}

static ODDS_UNUSED void die_message(const char *message) {
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static ODDS_UNUSED void *checked_malloc_array(size_t count, size_t size) {
    if (count != 0 && size > SIZE_MAX / count) {
        die_message("allocation size overflow");
    }

    void *ptr = malloc(count * size);
    if (ptr == NULL) {
        die_errno("malloc");
    }
    return ptr;
}

static ODDS_UNUSED void *checked_calloc_array(size_t count, size_t size) {
    if (count != 0 && size > SIZE_MAX / count) {
        die_message("allocation size overflow");
    }

    void *ptr = calloc(count, size);
    if (ptr == NULL) {
        die_errno("calloc");
    }
    return ptr;
}

static ODDS_UNUSED void *checked_realloc_array(void *ptr, size_t count, size_t size) {
    if (count != 0 && size > SIZE_MAX / count) {
        die_message("allocation size overflow");
    }

    void *new_ptr = realloc(ptr, count * size);
    if (new_ptr == NULL) {
        die_errno("realloc");
    }
    return new_ptr;
}

static ODDS_UNUSED void grow_numeric_array(numeric **arr, size_t *capacity) {
    if (*capacity > SIZE_MAX / 2) {
        die_message("array capacity overflow");
    }

    const size_t new_capacity = *capacity == 0 ? 1 : *capacity * 2;
    *arr = checked_realloc_array(*arr, new_capacity, sizeof(**arr));
    *capacity = new_capacity;
}

static ODDS_UNUSED size_t binary_search_insert_position(const numeric *arr, size_t size, numeric v) {
    size_t left = 0;
    size_t right = size;

    while (left < right) {
        const size_t mid = left + (right - left) / 2;

        if (arr[mid] < v) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    return left;
}

static ODDS_UNUSED bool divide_exact(numeric v, uint_fast8_t divisor, numeric *result) {
    const numeric quotient = (numeric)(v / divisor);

    if ((numeric)(quotient * divisor) != v) {
        return false;
    }

    *result = quotient;
    return true;
}

static ODDS_UNUSED bool mul2_add1(numeric value, numeric *result) {
    const numeric max_value = (numeric)~(numeric)0;

    if (value > (numeric)((max_value - 1) / 2)) {
        return false;
    }

    *result = (numeric)((value * (numeric)2) + (numeric)1);
    return true;
}

static ODDS_UNUSED void configure_stdout(void) {
    (void)setvbuf(stdout, NULL, _IOFBF, 1024 * 1024);
}

static ODDS_UNUSED void print_u128(const uint128_t v) {
    const uint64_t high = (uint64_t)(v >> 64);
    const uint64_t low = (uint64_t)v;

    printf("0x%016" PRIX64 "%016" PRIX64 "\n", high, low);
}

static ODDS_UNUSED void print_u64(const uint64_t v) {
    printf("0x%016" PRIX64 "\n", v);
}

static ODDS_UNUSED void print_u32(const uint32_t v) {
    printf("0x%08" PRIX32 "\n", v);
}

static ODDS_UNUSED void print_u16(const uint16_t v) {
    printf("0x%04" PRIX16 "\n", v);
}

#define PRINT_U(x) _Generic((x), \
    uint128_t: print_u128, \
    uint64_t: print_u64, \
    uint32_t: print_u32, \
    uint16_t: print_u16 \
)(x)
