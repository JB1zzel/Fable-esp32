# Linux build:  make        (then run ./fable; dict.smd must be next to it)
CC     ?= gcc
CFLAGS ?= -O2

fable: $(wildcard src/*.c) $(wildcard src/*.h)
	$(CC) $(CFLAGS) -o $@ src/*.c
	@echo "built ./fable - keep dict.smd in the same folder (cp src/dict.smd .)"

clean:
	rm -f fable

.PHONY: clean
