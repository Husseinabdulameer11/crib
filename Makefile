# Crib - a language for the maths of a cryptography course.
# Works with GNU make on Linux, macOS and Windows (MSYS2).
#
# Crib is built with gram, a separate parser library and generator
# (gramgen turns src/crib.gram into rule ids, and the parser is linked in).
# Get gram and do ONE of these:
#
#   1. install it:        cd gram && make install          (or PREFIX=~/.local)
#                         then just `make` here; gramgen is found on PATH
#   2. keep a checkout next to this one, as ../gram         (found automatically)
#   3. point at one:      make GRAM=path/to/gram
#                    or:  make GRAM_PREFIX=/where/gram/is/installed
CFLAGS  ?= -O2
LDFLAGS ?=
VERSION ?= dev
PREFIX  ?= /usr/local

ifeq ($(OS),Windows_NT)
  EXE := .exe
  ifeq ($(origin CC),default)
    CC := gcc
  endif
else
  EXE :=
endif

# ---- find gram --------------------------------------------------------------
ifeq ($(GRAM)$(GRAM_PREFIX),)
  GRAMGEN_ON_PATH := $(shell command -v gramgen 2>/dev/null)
  ifneq ($(GRAMGEN_ON_PATH),)
    # .../bin/gramgen -> ...
    GRAM_PREFIX := $(patsubst %/,%,$(dir $(patsubst %/,%,$(dir $(GRAMGEN_ON_PATH)))))
  else ifneq ($(wildcard ../gram/gram.c),)
    GRAM := ../gram
  endif
endif

ifneq ($(GRAM),)
  # a gram source checkout: build its library and gramgen into build/
  GRAMGEN  = build/gramgen$(EXE)
  GRAM_LIB = build/gram.o
  GRAM_INC = -I$(GRAM)
  GRAM_FROM = source checkout $(GRAM)
else ifneq ($(GRAM_PREFIX),)
  # an installed gram (make install)
  GRAMGEN  = $(GRAM_PREFIX)/bin/gramgen$(EXE)
  GRAM_LIB = $(GRAM_PREFIX)/lib/libgram.a
  GRAM_INC = -I$(GRAM_PREFIX)/include
  GRAM_FROM = installed in $(GRAM_PREFIX)
else
  GRAM_MISSING = 1
endif
# -----------------------------------------------------------------------------

CRIB  = build/crib$(EXE)
EMBED = build/embed$(EXE)
SRC   = src/crib.c src/crib_out.c src/crib_sim.c

all: $(CRIB)

build:
	mkdir -p build

need-gram:
ifdef GRAM_MISSING
	@echo "crib: gram was not found. Crib is built with the gram parser library."
	@echo "      Get gram, then either run 'make install' in it, put it next to"
	@echo "      this folder as ../gram, or run: make GRAM=path/to/gram"
	@exit 1
else
	@echo "crib: using gram ($(GRAM_FROM))"
endif

ifneq ($(GRAM),)
build/gram.o: $(GRAM)/gram.c $(GRAM)/gram.h | build
	$(CC) $(CFLAGS) -c $(GRAM)/gram.c -o $@

$(GRAMGEN): $(GRAM)/tools/gramgen.c build/gram.o
	$(CC) $(CFLAGS) $(GRAM)/tools/gramgen.c build/gram.o -o $@
endif

$(EMBED): tools/embed.c | build
	$(CC) $(CFLAGS) -Wall -Wextra tools/embed.c -o $@

# grammar -> rule ids and grammar text (build/crib_grammar.h)
build/crib_grammar.h: src/crib.gram $(GRAMGEN) | need-gram build
	$(GRAMGEN) src/crib.gram crib build

# the HTML viewer, baked into the binary as a string
build/crib_view.h: src/crib_view.html $(EMBED)
	$(EMBED) src/crib_view.html crib_view > $@

$(CRIB): $(SRC) src/crib.h build/crib_grammar.h build/crib_view.h $(GRAM_LIB) | need-gram
	$(CC) $(CFLAGS) -Wall -Wextra -DCRIB_VERSION='"$(VERSION)"' -Isrc -Ibuild $(GRAM_INC) \
	    $(SRC) $(GRAM_LIB) $(LDFLAGS) -o $@

test: $(CRIB)
	$(CRIB) examples/exam.crib | diff --strip-trailing-cr tests/exam.expected -
	$(CRIB) examples/language.crib > /dev/null
	$(CRIB) --html build/exam.html examples/exam.crib > /dev/null
	@echo "crib: all tests passed"

# Linux/macOS: installs build/crib as $(PREFIX)/bin/crib
install: $(CRIB)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(CRIB) $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/crib$(EXE)

clean:
	rm -rf build

.PHONY: all test install uninstall clean need-gram
