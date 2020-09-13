PREFIX ?= /usr/local

HTMLS := $(patsubst %.adoc,%.html,$(wildcard *.adoc))
MANUALS := xsys35c.1 xsys35dc.1 ald.1

all: html man
html: $(HTMLS)
man: $(MANUALS)

%.html: %.adoc
	asciidoctor $<

%.1: %.adoc
	asciidoctor -b manpage $<

install: $(MANUALS)
	mkdir -p $(PREFIX)/share/man/man1
	cp $(MANUALS) $(PREFIX)/share/man/man1/

clean:
	rm -f $(HTMLS) $(MANUALS)

.PHONY: all html man clean install
