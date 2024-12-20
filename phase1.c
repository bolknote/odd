#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#define MASK64(v) (uint64_t) ((v) & 0xFFFFFFFFFFFFFFFF)
#define INITIAL_CAPACITY 1024

typedef unsigned _BitInt(128) uint128_t;

uint128_t* odds = NULL;
size_t odds_size = 0;
size_t odds_capacity = 0;

uint128_t *stack = NULL;
size_t stack_size = 0;
size_t stack_capacity = 0;

uint128_t is_divided_by(uint128_t v, uint_fast8_t by) {
	uint128_t result = v / by;
	return result * by == v ? result : 0;
}

void printf_u128(const uint128_t v) {
	uint64_t low, high;

    bool is_little_endian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;

    if (is_little_endian) {
        low  = MASK64(v);
        high = MASK64(v >> 64);
    } else {
        high = MASK64(v >> 64);
        low  = MASK64(v);
    }

    printf("0x%016llX%016llX\n", high, low);
    fflush(stdout);
}

int compare_uint128_t(const void *a, const void *b) {
    if (*(uint128_t*) a < *(uint128_t*) b) return -1;
    if (*(uint128_t*) a > *(uint128_t*) b) return 1;
    return 0;
}

size_t binary_search_insert_position(const uint128_t *arr, size_t size, uint128_t v) {
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

void realloc_stack(uint128_t **stack, size_t *capacity) {
    *capacity *= 2;

    *stack = realloc(*stack, *capacity * sizeof(uint128_t));

    if (*stack == NULL) {
        exit(1);
    }
}

bool check_exists_and_add(const uint128_t v) {
	size_t pos = 0;

	if (odds_size) {
	    pos = binary_search_insert_position(odds, odds_size, v);

	    if (pos < odds_size && odds[pos] == v) {
	        return true;
	    }
	}

    if (odds_size == odds_capacity) {
    	realloc_stack(&odds, &odds_capacity);
	}

	memmove(&odds[pos + 1], &odds[pos], (odds_size - pos) * sizeof(uint128_t));

	odds[pos] = v;
	odds_size++;

	return false;
}

void push_stack(uint128_t value) {
    if (stack_size == stack_capacity) {
    	realloc_stack(&stack, &stack_capacity);
    }

    stack[stack_size++] = value;
}

uint128_t pop_stack() {
    return stack[--stack_size];
}

void main_enumeration(uint128_t counter) {
	while (counter != (uint128_t) ~0) {
		if (check_exists_and_add(counter)) {
			return;
		}

		printf_u128(counter);

		uint128_t cnt_by_3 = is_divided_by(counter, 3);

		if (cnt_by_3) {
			do {
				if (check_exists_and_add(cnt_by_3)) {
					break;
				}

				printf_u128(cnt_by_3);
				cnt_by_3 = is_divided_by(cnt_by_3, 3);

				if (cnt_by_3) {
					push_stack(cnt_by_3 * 2 + 1);
				}
			} while (cnt_by_3);
		}

		counter = 2 * counter + 1;
	}
}

int main(void) {
	odds_capacity = INITIAL_CAPACITY;
	odds = malloc(odds_capacity * sizeof(uint128_t));

	stack_capacity = INITIAL_CAPACITY;
	stack = malloc(stack_capacity * sizeof(uint128_t));

	push_stack(1);

	while (stack_size > 0) {
		main_enumeration(pop_stack());
	}

	free(stack);
	free(odds);

	return 0;
}
