.PHONY: all release debug clean

GCC_OPTS = -Wall -Wshadow --pedantic -Wvla
DEBUG_OPTS = -Werror -pg
REL_OPTS = -O3
GCC = gcc $(GCC_OPTS)

SRC_DIR = ./src

SRCS := $(wildcard $(SRC_DIR)/*.c)

all: release

release:
	$(GCC) $(REL_OPTS) $(SRCS) -o clox

debug:
	$(GCC) $(DEBUG_OPTS) $(SRCS) -o clox


clean:
	rm -fr ./clox
