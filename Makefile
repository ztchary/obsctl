CFLAGS=-Wall -Werror `pkg-config --cflags json-c libwebsockets`
LIBS=`pkg-config --libs json-c libwebsockets` -lcrypto

obsctl: *.c commands.h
	gcc $(CFLAGS) -o obsctl *.c $(LIBS)

protocol.json:
	wget https://raw.githubusercontent.com/obsproject/obs-websocket/refs/heads/master/docs/generated/protocol.json

commands.h: protocol.json gen_commands.py
	python3 gen_commands.py

cleanup:
	rm protocol.json
	rm commands.h

