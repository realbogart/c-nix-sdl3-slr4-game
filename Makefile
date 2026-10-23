# NOCTIS - plain C on SDL3 (plus libc + libm).
#
# Inside `nix develop`, CC and PKG_CONFIG run inside the Steam Linux Runtime 4
# SDK and RUNNER runs the game inside the Steam Linux Runtime 4 container, so
# `make run` behaves like the Steam release. Outside it, any C compiler with
# SDL3 development files works.
CC         ?= cc
PKG_CONFIG ?= pkg-config
PREFIX     ?= /usr/local
RUNNER     ?=
CFLAGS     ?= -O2 -g
# Required flags live in their own variables so `make CFLAGS=...` only changes
# optimisation/debug flags.
BASE_CFLAGS := -std=c11 -Wall -Wextra -Wshadow -Wno-unused-parameter -D_DEFAULT_SOURCE
SDL_CFLAGS  := $(shell $(PKG_CONFIG) --cflags sdl3)
SDL_LIBS    := $(shell $(PKG_CONFIG) --libs sdl3)

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)

.PHONY: all run clean install shots

all: noctis

noctis: $(OBJ)
	$(CC) $(BASE_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(SDL_LIBS) -lm $(LDLIBS)

build/%.o: src/%.c src/common.h | build
	$(CC) $(BASE_CFLAGS) $(SDL_CFLAGS) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

run: noctis
	$(RUNNER) ./noctis $(ARGS)

# Headless screenshots of each chapter (writes build/shot-*.ppm)
shots: noctis
	$(RUNNER) ./noctis --shot build/shot-title.ppm --title --frames 30
	$(RUNNER) ./noctis --shot build/shot-1.ppm --level 1 --frames 60
	$(RUNNER) ./noctis --shot build/shot-2.ppm --level 2 --frames 60
	$(RUNNER) ./noctis --shot build/shot-3.ppm --level 3 --frames 60

install: noctis
	install -Dm755 noctis $(DESTDIR)$(PREFIX)/bin/noctis

clean:
	rm -rf build noctis
