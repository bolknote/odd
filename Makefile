# Default compiler (override: make CC=clang)
CC = gcc
NUMERIC_TYPE ?= uint32_t
CPPFLAGS += -DNUMERIC_TYPE=$(NUMERIC_TYPE)
DEPFLAGS = -MMD -MP

# --- Development build: strict warnings + moderate optimization + hardening ---
WARN_DEV = -Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Wconversion -Wsign-conversion -Wnull-dereference -Wstrict-prototypes -Wmissing-prototypes
OPT_DEV  = -O2 -flto
SEC_DEV  = -fstack-protector-strong -D_FORTIFY_SOURCE=2

CFLAGS_DEV = $(WARN_DEV) $(OPT_DEV) $(SEC_DEV)

# --- Fast benchmark build: higher optimization, tuned for this CPU; lighter warnings ---
# Override tuning if needed, e.g.: make fast MARCH=znver3
MARCH ?= native
WARN_FAST = -Wall -Wextra
OPT_FAST  = -O3 -march=$(MARCH) -flto

CFLAGS_FAST = $(WARN_FAST) $(OPT_FAST)

.PHONY: all fast clean FORCE

all: phase1 phase2

fast: phase1-fast phase2-fast

phase1: phase1.c common.h FORCE
	$(CC) $(CPPFLAGS) $(CFLAGS_DEV) $(DEPFLAGS) -MF phase1.d -o phase1 phase1.c

phase2: phase2.c common.h FORCE
	$(CC) $(CPPFLAGS) $(CFLAGS_DEV) $(DEPFLAGS) -MF phase2.d -pthread -o phase2 phase2.c

phase1-fast: phase1.c common.h FORCE
	$(CC) $(CPPFLAGS) $(CFLAGS_FAST) $(DEPFLAGS) -MF phase1-fast.d -o phase1-fast phase1.c

phase2-fast: phase2.c common.h FORCE
	$(CC) $(CPPFLAGS) $(CFLAGS_FAST) $(DEPFLAGS) -MF phase2-fast.d -pthread -o phase2-fast phase2.c

clean:
	rm -f phase1 phase2 phase1-fast phase2-fast phase1.d phase2.d phase1-fast.d phase2-fast.d

FORCE:

-include phase1.d phase2.d phase1-fast.d phase2-fast.d
