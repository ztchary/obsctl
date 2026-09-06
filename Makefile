CFLAGS=-Wall -Werror `pkg-config --cflags json-c libwebsockets`
LIBS=`pkg-config --libs json-c libwebsockets` -lcrypto

.PHONY: clean all

all: obsctl

obsctl: *.c commands.h
	gcc $(CFLAGS) -o obsctl *.c $(LIBS)

protocol.json:
	wget https://raw.githubusercontent.com/obsproject/obs-websocket/refs/heads/master/docs/generated/protocol.json

commands.h: protocol.json gen_commands.py
	python3 gen_commands.py

readme: obsctl
	echo -e "# obsctl\nauto-generated readme because idc\n---\n" > README.md
	obsctl --help | sed 's/^  /- /' >> README.md

clean:
	rm -f protocol.json
	rm -f commands.h
	rm -f obsctl

