CC = gcc
CFLAGS = -Wall -lrt -O2

# Get a list of all .c files in the current directory
SOURCES := $(wildcard *.c)

# Generate a list of corresponding executable names
EXECUTABLES := $(SOURCES:.c=)

# Default target to build all executables
all: $(EXECUTABLES)

# Rule to build an executable from a .c file
%: %.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(EXECUTABLES)

.PHONY: all clean
