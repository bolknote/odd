CC = gcc
CFLAGS = -Ofast -Wall -Wextra

all: phase1 phase2

phase1: phase1.c
	$(CC) $(CFLAGS) -o phase1 phase1.c

phase2: phase2.c
	$(CC) $(CFLAGS) -o phase2 phase2.c

clean:
	rm -f phase1 phase2
