CFLAGS = -std=c11 -Wall -g
LDFLAGS = -liconv
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

xsys35c: xsys35c.o compile.o sco.o ald.o util.o

$(OBJS): xsys35c.h

compile_test: compile_test.o compile.o sco.o util.o
ald_test: ald_test.o ald.o util.o
sco_test: sco_test.o sco.o util.o

test: compile_test ald_test sco_test
	./sco_test
	./compile_test
	./ald_test && cmp testdata/expected.ald testdata/actual.ald && rm testdata/actual.ald

clean:
	rm -rf *.o xsys35c compile_test ald_test sco_test

.PHONY: test
