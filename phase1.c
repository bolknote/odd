#include "common.h"

#define INITIAL_CAPACITY 1024

static numeric *stack = NULL;
static size_t stack_size = 0;
static size_t stack_capacity = 0;
static VisitedSet visited;

static bool check_exists_and_add(const numeric v) {
    return visited_check_and_add(&visited, v);
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
    visited_init(&visited);

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
    visited_destroy(&visited);

    return 0;
}
