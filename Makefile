CFLAGS = -std=c11 -Wall -O2 -Icommon
LDFLAGS = -liconv

COMMON_SRCS := \
	common/ald.c \
	common/util.c

COMMON_OBJS=$(COMMON_SRCS:.c=.o)

COMPILER_SRCS= \
	compiler/ain.c \
	compiler/compile.c \
	compiler/lexer.c \
	compiler/sco.c

COMPILER_OBJS=$(COMPILER_SRCS:.c=.o)

compiler/xsys35c: compiler/xsys35c.o $(COMPILER_OBJS) $(COMMON_OBJS)

$(COMMON_OBJS): common/common.h
$(COMPILER_OBJS): compiler/xsys35c.h common/common.h

common/ald_test: common/ald_test.o $(COMMON_OBJS)
compiler/compile_test: compiler/compile_test.o $(COMPILER_OBJS) $(COMMON_OBJS)
compiler/sco_test: compiler/sco_test.o $(COMPILER_OBJS) $(COMMON_OBJS)

test: common/ald_test compiler/compile_test compiler/sco_test compiler/xsys35c regression_test.sh
	compiler/sco_test
	compiler/compile_test
	common/ald_test && cmp testdata/expected.ald testdata/actual.ald && rm testdata/actual.ald
	./regression_test.sh

clean:
	rm -rf *.o common/*.o compiler/*.o compiler/xsys35c common/ald_test compiler/compile_test compiler/sco_test

.PHONY: clean test
