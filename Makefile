CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lm

SRC     = src/ltw_encoder.c src/ltw_decoder.c src/dwt.c src/arith_coder.c
OBJ     = $(SRC:.c=.o)

.PHONY: all test clean

all: ltw

ltw: $(OBJ) src/main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/main.o: src/main.c
	$(CC) $(CFLAGS) -c -o $@ $<

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: test/test_8x8
	./test/test_8x8

test/test_8x8: test/test_8x8.o $(SRC:.c=.o)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test/test_8x8.o: test/test_8x8.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o test/*.o ltw test/test_8x8
