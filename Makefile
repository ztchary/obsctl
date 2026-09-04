CFLAGS=-Wall -Werror `pkg-config --cflags json-c libwebsockets`
LIBS=`pkg-config --libs json-c libwebsockets`

obsctl: *.c usage.h
	gcc $(CFLAGS) -o obsctl *.c $(LIBS)

usage.h: usage.txt
	xxd -i usage.txt > usage.h
