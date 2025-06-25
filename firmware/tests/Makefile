CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2
LDFLAGS = -ludev

PROTOCOL_SRC = gripdeck_protocol.c
TEST_SRC = gripdeck_test.c

PROTOCOL_OBJ = $(PROTOCOL_SRC:.c=.o)
TEST_OBJ = $(TEST_SRC:.c=.o)

TEST_EXEC = gripdeck_test

all: $(TEST_EXEC)

$(TEST_EXEC): $(PROTOCOL_OBJ) $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c gripdeck_protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TEST_EXEC)