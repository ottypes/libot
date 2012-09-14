.PHONY: all clean

LIBROPE=../librope
CFLAGS=-O2 -emit-llvm -Wall -arch x86_64 -I. -I$(LIBROPE)

all: libot.a

clean:
	rm -f libot.a *.o test

libot.a: text-composable.o str.o utf8.o
	cp $(LIBROPE)/librope.a libot.a
	ar -r $@ $+

# Only need corefoundation to run the tests on mac
test: libot.a test.c 
	$(CC) $(CFLAGS) $+ -o $@

