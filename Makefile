SRC = odds.c
OUT = odds

CC = gcc
CFLAGS = -Ofast -Wall -Wextra

all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(OUT)

clean:
	rm -f $(OUT)
