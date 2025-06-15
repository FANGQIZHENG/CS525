CC = gcc
CFLAGS = -Wall -g -std=c99 -Dbool=_Bool

# Source files and generated objects
BASE_SRCS = storage_mgr.c buffer_mgr.c dberror.c buffer_mgr_stat.c
BASE_OBJS = $(BASE_SRCS:.c=.o)

# Test programs
tests = test_assign2_1 test_assign2_2

# Default target: build both tests
all: $(tests)

# Link rule for test_assign2_1
test_assign2_1: $(BASE_OBJS) test_assign2_1.o
	$(CC) $(CFLAGS) -o $@ $^

# Link rule for test_assign2_2
test_assign2_2: $(BASE_OBJS) test_assign2_2.o
	$(CC) $(CFLAGS) -o $@ $^

# Compile .c to .o
%.o: %.c
	$(CC) $(CFLAGS) -c $<

# Clean up build artifacts
clean:
	rm -f $(BASE_OBJS) test_assign2_1.o test_assign2_2.o $(tests)
