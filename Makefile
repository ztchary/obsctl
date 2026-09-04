CFLAGS=-Wall -Werror `pkg-config --cflags json-c libwebsockets`
LIBS=`pkg-config --libs json-c libwebsockets`

obsctl: *.c
	gcc $(CFLAGS) -o obsctl *.c $(LIBS)
