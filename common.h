#pragma once

#include <stdbool.h>
#include <limits.h>
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

#define NUMERIC_BITS (sizeof(numeric) * CHAR_BIT)
#define DENSE_BITSET_MAX_BITS 32
#define BITSET_PAGE_BITS 12
#define BITSET_PAGE_VALUES ((size_t)1 << BITSET_PAGE_BITS)
#define BITSET_WORD_BITS 64
#define BITSET_PAGE_WORDS (BITSET_PAGE_VALUES / BITSET_WORD_BITS)
#define SPARSE_BUCKET_COUNT 65536

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

static ODDS_UNUSED uint64_t hash_numeric(numeric value) {
    const uint128_t wide = (uint128_t)value;
    uint64_t h = (uint64_t)wide ^ (uint64_t)(wide >> 64);

    h ^= h >> 30;
    h *= UINT64_C(0xbf58476d1ce4e5b9);
    h ^= h >> 27;
    h *= UINT64_C(0x94d049bb133111eb);
    h ^= h >> 31;

    return h;
}

typedef struct BitsetPage {
    numeric page_id;
    uint64_t words[BITSET_PAGE_WORDS];
    struct BitsetPage *next;
} BitsetPage;

typedef struct {
    bool dense;
    uint64_t *dense_words;
    size_t dense_word_count;
    BitsetPage **sparse_buckets;
} VisitedSet;

static ODDS_UNUSED void visited_init(VisitedSet *set) {
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
}

static ODDS_UNUSED void visited_destroy(VisitedSet *set) {
    free(set->dense_words);

    if (set->sparse_buckets != NULL) {
        for (size_t i = 0; i < SPARSE_BUCKET_COUNT; i++) {
            BitsetPage *page = set->sparse_buckets[i];

            while (page != NULL) {
                BitsetPage *next = page->next;
                free(page);
                page = next;
            }
        }

        free(set->sparse_buckets);
    }

    set->dense_words = NULL;
    set->dense_word_count = 0;
    set->sparse_buckets = NULL;
}

static ODDS_UNUSED bool visited_check_and_add(VisitedSet *set, numeric value) {
    const numeric odd_index = (numeric)(value >> 1);

    if (set->dense) {
        const size_t bit_index = (size_t)odd_index;
        const size_t word_index = bit_index / BITSET_WORD_BITS;
        const uint64_t mask = UINT64_C(1) << (bit_index % BITSET_WORD_BITS);
        const bool exists = (set->dense_words[word_index] & mask) != 0;

        set->dense_words[word_index] |= mask;
        return exists;
    }

    const numeric page_id = (numeric)(odd_index >> BITSET_PAGE_BITS);
    const unsigned page_offset = (unsigned)(odd_index & (numeric)(BITSET_PAGE_VALUES - 1));
    const size_t bucket_index = (size_t)(hash_numeric(page_id) % SPARSE_BUCKET_COUNT);
    BitsetPage *page = set->sparse_buckets[bucket_index];

    while (page != NULL && page->page_id != page_id) {
        page = page->next;
    }

    if (page == NULL) {
        page = checked_calloc_array(1, sizeof(*page));
        page->page_id = page_id;
        page->next = set->sparse_buckets[bucket_index];
        set->sparse_buckets[bucket_index] = page;
    }

    const size_t word_index = page_offset / BITSET_WORD_BITS;
    const uint64_t mask = UINT64_C(1) << (page_offset % BITSET_WORD_BITS);
    const bool exists = (page->words[word_index] & mask) != 0;

    page->words[word_index] |= mask;
    return exists;
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

static ODDS_UNUSED void print_count(uintmax_t count) {
    printf("%" PRIuMAX "\n", count);
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
