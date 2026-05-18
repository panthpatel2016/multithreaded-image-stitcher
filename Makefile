# Makefile, ECE252 Lab 3

CC = gcc
CFLAGS = -Wall -g -std=c99
LDLIBS = -lcurl -lz -pthread

# Source Dir
SRC_DIR = src

# Main Source files
SRC = $(SRC_DIR)/paster2.c $(SRC_DIR)/lab_png.c $(SRC_DIR)/crc.c $(SRC_DIR)/zutil.c $(SRC_DIR)/catpng.c

# Targets
TARGETS = paster2

all: ${TARGETS}

paster2: $(SRC)
	$(CC) -o $(TARGETS) $(SRC) $(LDLIBS)

.PHONY: clean all

clean:
	rm -f $(TARGETS)