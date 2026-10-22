# NOCTIS - plain C, no external libraries (only libc + libm).
CC      ?= cc
PREFIX  ?= /usr/local
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wshadow -Wno-unused-parameter -D_DEFAULT_SOURCE
LDLIBS  += -lm

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all run clean install shots

all: noctis

noctis: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c src/common.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

run: noctis
	./noctis

# Headless screenshots of each chapter (writes build/shot-*.ppm)
shots: noctis
	./noctis --shot build/shot-title.ppm --title --frames 30
	./noctis --shot build/shot-1.ppm --level 1 --frames 60
	./noctis --shot build/shot-2.ppm --level 2 --frames 60
	./noctis --shot build/shot-3.ppm --level 3 --frames 60

install: noctis
	install -Dm755 noctis $(DESTDIR)$(PREFIX)/bin/noctis

clean:
	rm -rf build noctis
