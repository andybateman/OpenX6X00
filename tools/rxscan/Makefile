# Host test of the receive scan (src/rxscan.c): `make` builds and runs it. No radio and no screen: the band is a
# list of readings and the clock the test's own.
SRC    := ../../src
CFLAGS := -std=gnu11 -Wall -Wextra -O1 -g -I $(SRC)

all: test

rxscan_test: rxscan_test.c $(SRC)/rxscan.c $(SRC)/rxscan.h
	$(CC) $(CFLAGS) -o $@ rxscan_test.c $(SRC)/rxscan.c -lm

test: rxscan_test
	./rxscan_test

clean:
	rm -f rxscan_test

.PHONY: all test clean
