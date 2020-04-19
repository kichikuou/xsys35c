CFLAGS = -Wall -O2 -Icommon
ifeq ($(shell uname), Darwin)
	LDFLAGS = -liconv
endif

COMMON_SRCS := \
	common/ald.c \
	common/util.c

COMMON_OBJS=$(COMMON_SRCS:.c=.o)

COMPILER_SRCS= \
	compiler/ain.c \
	compiler/compile.c \
	compiler/config.c \
	compiler/hel.c \
	compiler/lexer.c \
	compiler/sco.c

COMPILER_OBJS=$(COMPILER_SRCS:.c=.o)

DECOMPILER_SRCS= \
	decompiler/ain.c \
	decompiler/cali.c \
	decompiler/decompile.c

DECOMPILER_OBJS=$(DECOMPILER_SRCS:.c=.o)

all: compiler/xsys35c decompiler/xsys35dc tools/ald

$(COMMON_OBJS): common/common.h
$(COMPILER_OBJS): compiler/xsys35c.h common/common.h
$(DECOMPILER_OBJS): decompiler/xsys35dc.h common/common.h

compiler/xsys35c: compiler/xsys35c.o $(COMPILER_OBJS) $(COMMON_OBJS)
decompiler/xsys35dc: decompiler/xsys35dc.o $(DECOMPILER_OBJS) $(COMMON_OBJS)
tools/ald: tools/ald.o $(COMMON_OBJS)

common/ald_test: common/ald_test.o $(COMMON_OBJS)
compiler/compile_test: compiler/compile_test.o $(COMPILER_OBJS) $(COMMON_OBJS)
compiler/sco_test: compiler/sco_test.o $(COMPILER_OBJS) $(COMMON_OBJS)
compiler/hel_test: compiler/hel_test.o $(COMPILER_OBJS) $(COMMON_OBJS)

test: common/ald_test compiler/compile_test compiler/sco_test compiler/hel_test compiler/xsys35c regression_test.sh
	compiler/sco_test
	compiler/hel_test
	compiler/compile_test
	common/ald_test && cmp testdata/expected.ald testdata/actual.ald && rm testdata/actual*.ald
	./regression_test.sh

clean:
	rm -rf *.o common/*.o compiler/*.o decompiler/*.o tools/*.o compiler/xsys35c decompiler/xsys35dc tools/ald common/ald_test compiler/compile_test compiler/sco_test compiler/hel_test

.PHONY: clean test
