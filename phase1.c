#include <string.h>

#include "common.h"

#define INITIAL_CAPACITY 1024

static numeric *stack = NULL;
static size_t stack_size = 0;
static size_t stack_capacity = 0;
static VisitedSet visited;
static bool count_only = false;
static uintmax_t emitted_count = 0;

static void parse_args(int argc, char *argv[]) {
    if (argc == 1) {
        return;
    }

    if (argc == 2 && strcmp(argv[1], "--count") == 0) {
        count_only = true;
        return;
    }

    die_message("usage: phase1 [--count]");
}

static void emit_value(numeric value) {
    emitted_count++;

    if (!count_only) {
        PRINT_U(value);
    }
}

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
        emit_value(value);
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

int main(int argc, char *argv[]) {
    parse_args(argc, argv);
    configure_stdout();
    visited_init(&visited);

    stack_capacity = INITIAL_CAPACITY;
    stack = checked_malloc_array(stack_capacity, sizeof(*stack));

    const numeric seed = 1;
    if (!check_exists_and_add(seed)) {
        emit_value(seed);
        push_stack(seed);
    }

    while (stack_size > 0) {
        process_value(pop_stack());
    }

    free(stack);
    visited_destroy(&visited);

    if (count_only) {
        print_count(emitted_count);
    }

    return 0;
}
