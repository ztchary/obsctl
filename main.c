#include <json-c/json.h>
#include <libwebsockets.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

int main(int argc, char **argv) {
	char *server = getenv("OBS_SERVER");
	char *passwd = getenv("OBS_PASSWD");

	int opt;

	while ((opt = getopt(argc, argv, "s:p:")) != -1) {
		switch (opt) {
			case 's': server = optarg; break;
			case 'p': passwd = optarg; break;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "provide a command\n");
		return 1;
	}

	if (!server) server = "ws://localhost:4455";
	if (strncmp(server, "ws://", 5) != 0 || strchr(server+5, ':') == NULL) {
		fprintf(stderr, "server must be a valid websocket address\n");
		return 1;
	}

	printf("%s\n", server);
	printf("%s\n", passwd ? passwd : "none");
}
