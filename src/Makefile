# USB Mouse Jiggler Makefile
# Compile for RG34XXSP (aarch64) under muOS

CC ?= gcc
CFLAGS = -O2 -Wall -std=c99 -D_GNU_SOURCE
LDFLAGS = -lSDL2 -lpthread

TARGET = ../usbjiggler
SRC = usbjiggler.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

# Cross-compile for aarch64:
# make CC=aarch64-linux-gnu-gcc
# Or with KNULLI SDK:
# make CC=aarch64-buildroot-linux-gnu-gcc

.PHONY: all clean
