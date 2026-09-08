CFLAGS=-Wall -Werror -std=c99 -pedantic `pkg-config --cflags json-c libwebsockets`
LIBS=`pkg-config --libs json-c libwebsockets` -lcrypto

.PHONY: clean all

all: obsctl

obsctl: *.c commands.h
	gcc $(CFLAGS) -o obsctl *.c $(LIBS)

commands.h: gen_commands.py
	python3 gen_commands.py

readme: obsctl
	echo -e "# obsctl\nauto-generated readme because idc\n---\n" > README.md
	obsctl --help | sed 's/^  /- /' >> README.md

install: obsctl
	install -m 755 obsctl /usr/bin

clean:
	rm -f commands.h
	rm -f obsctl

