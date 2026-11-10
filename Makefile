# Requires a C compiler, pkg-config, and raylib.
# On macOS: brew install pkg-config raylib
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
LDLIBS += -lm

RAYLIB_CFLAGS = $(shell $(PKG_CONFIG) --cflags raylib)
RAYLIB_LIBS = $(shell $(PKG_CONFIG) --libs raylib)

.PHONY: all run clean check-deps

all: cube

cube: cube.c Makefile | check-deps
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RAYLIB_CFLAGS) cube.c $(LDFLAGS) -o $@ $(RAYLIB_LIBS) $(LDLIBS)

check-deps:
	@command -v $(PKG_CONFIG) >/dev/null 2>&1 || { echo "Error: install pkg-config (macOS: brew install pkg-config raylib)." >&2; exit 1; }
	@$(PKG_CONFIG) --exists raylib || { echo "Error: install raylib or set PKG_CONFIG_PATH to its pkgconfig directory (macOS: brew install raylib)." >&2; exit 1; }

run: cube
	./cube

clean:
	$(RM) cube
