.PHONY: all clean

LIBROPE=../librope
CFLAGS=-O2 -Wall -I. -I$(LIBROPE)

UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
CFLAGS := $(CFLAGS) -emit-llvm -arch x86_64
endif

all: libot.a

clean:
	rm -f libot.a *.o test

$(LIBROPE)/librope.a:
	$(MAKE) librope.a -C$(LIBROPE)

libot.a: $(LIBROPE)/librope.a text.o str.o utf8.o
	cp $(LIBROPE)/librope.a libot.a
	ar rs $@ $+

# Only need corefoundation to run the tests on mac
test: libot.a test.c 
	$(CC) $(CFLAGS) $+ -o $@

