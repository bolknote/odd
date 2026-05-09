#include <string.h>

#include "common.h"

#define INITIAL_CAPACITY 1024

static numeric *odds = NULL;
static size_t odds_size = 0;
static size_t odds_capacity = 0;

static numeric *stack = NULL;
static size_t stack_size = 0;
static size_t stack_capacity = 0;

static bool check_exists_and_add(const numeric v) {
    const size_t pos = binary_search_insert_position(odds, odds_size, v);

    if (pos < odds_size && odds[pos] == v) {
        return true;
    }

    if (odds_size + 1 >= odds_capacity) {
        grow_numeric_array(&odds, &odds_capacity);
    }

    memmove(&odds[pos + 1], &odds[pos], (odds_size - pos) * sizeof(numeric));

    odds[pos] = v;
    odds_size++;

    return false;
}

static void push_stack(numeric value) {
    if (stack_size == stack_capacity) {
        grow_numeric_array(&stack, &stack_capacity);
    }

    stack[stack_size++] = value;
}

static inline numeric pop_stack(void) {
    return stack[--stack_size];
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

int main(void) {
    configure_stdout();

    odds_capacity = INITIAL_CAPACITY;
    odds = checked_malloc_array(odds_capacity, sizeof(*odds));

    stack_capacity = INITIAL_CAPACITY;
    stack = checked_malloc_array(stack_capacity, sizeof(*stack));

    const numeric seed = 1;
    if (!check_exists_and_add(seed)) {
        PRINT_U(seed);
        push_stack(seed);
    }

    while (stack_size > 0) {
        process_value(pop_stack());
    }

    free(stack);
    free(odds);

    return 0;
}
