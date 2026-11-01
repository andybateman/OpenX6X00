# Host test of the Prop app's logic (src/propband.c): `make` builds and runs it. No radio, no screen, no network.
SRC    := ../../src
CFLAGS := -std=gnu11 -Wall -Wextra -O1 -g -I $(SRC)

all: test

propband_test: propband_test.c $(SRC)/propband.c $(SRC)/propband.h $(SRC)/qth/qth.c
	$(CC) $(CFLAGS) -o $@ propband_test.c $(SRC)/propband.c $(SRC)/qth/qth.c -lm

test: propband_test
	./propband_test

clean:
	rm -rf propband_test propband_test.dSYM

.PHONY: all test clean
