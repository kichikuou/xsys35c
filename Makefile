CFLAGS = -Wall -Werror -O2 -Icommon

PREFIX ?= /usr/local

COMMANDS := \
	compiler/xsys35c \
	decompiler/xsys35dc \
	tools/ald \
	tools/vsp \
	tools/pms \
	tools/qnt

TESTS := \
	common/common_tests \
	compiler/compiler_tests

COMMON_OBJS := \
	common/ald.o \
	common/container.o \
	common/sjisutf.o \
	common/util.o

COMMON_TESTS_OBJS := \
	common/ald_test.o \
	common/common_tests.o \
	common/sjisutf_test.o

COMPILER_OBJS := \
	compiler/ain.o \
	compiler/compile.o \
	compiler/config.o \
	compiler/hel.o \
	compiler/lexer.o \
	compiler/sco.o

COMPILER_TESTS_OBJS := \
	compiler/compile_test.o \
	compiler/compiler_tests.o \
	compiler/hel_test.o \
	compiler/sco_test.o

DECOMPILER_OBJS := \
	decompiler/ain.o \
	decompiler/cali.o \
	decompiler/decompile.o \
	decompiler/preprocess.o

MAIN_OBJS := \
	compiler/xsys35c.o \
	decompiler/xsys35dc.o \
	tools/ald.o \
	tools/vsp.o \
	tools/pms.o \
	tools/qnt.o

all: $(COMMANDS)

$(COMMON_OBJS) $(COMMON_TESTS_OBJS) $(MAIN_OBJS): common/common.h
$(COMPILER_OBJS) $(COMPILER_TESTS_OBJS): compiler/xsys35c.h common/common.h
$(DECOMPILER_OBJS): decompiler/xsys35dc.h common/common.h
common/sjisutf.o: common/s2utbl.h

compiler/xsys35c: compiler/xsys35c.o $(COMPILER_OBJS) $(COMMON_OBJS)
decompiler/xsys35dc: decompiler/xsys35dc.o $(DECOMPILER_OBJS) $(COMMON_OBJS)
tools/ald: tools/ald.o $(COMMON_OBJS)
tools/vsp: tools/vsp.o $(COMMON_OBJS) tools/png_utils.o
	$(CC) $^ -o $@ $(STATIC) -lpng -lz
tools/pms: tools/pms.o $(COMMON_OBJS) tools/png_utils.o
	$(CC) $^ -o $@ $(STATIC) -lpng -lz
tools/qnt: tools/qnt.o $(COMMON_OBJS) tools/png_utils.o
	$(CC) $^ -o $@ $(STATIC) -lpng -lz

common/common_tests: $(COMMON_TESTS_OBJS) $(COMMON_OBJS)
compiler/compiler_tests: $(COMPILER_TESTS_OBJS) $(COMPILER_OBJS) $(COMMON_OBJS)

test: $(TESTS) $(COMMANDS) regression_test.sh
	common/common_tests && cmp testdata/expected.ald testdata/actual.ald && rm testdata/actual*.ald
	compiler/compiler_tests
	./regression_test.sh

install: $(COMMANDS)
	mkdir -p $(PREFIX)/bin
	cp $(COMMANDS) $(PREFIX)/bin/

install-man:
	$(MAKE) -C docs install

clean:
	rm -rf *.o common/*.o compiler/*.o decompiler/*.o tools/*.o $(COMMANDS) $(TESTS)

.PHONY: all clean install install-man test
