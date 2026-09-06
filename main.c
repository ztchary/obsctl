#include <libwebsockets.h>
#include <json-c/json.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "commands.h"

bool run = true;
int ret = 1;
char *server;
char *passwd;
char **req;

int compute_auth(const char *pass, const char *salt, const char *chal, char *out) {
	char concat[256];
	char hash[32] = {0};
	char secret[64];
	int len;

	len = snprintf(concat, sizeof(concat), "%s%s", pass, salt);
	SHA256((unsigned char *)concat, len, (unsigned char *)hash);
	lws_b64_encode_string(hash, 32, secret, sizeof(secret));

	len = snprintf(concat, sizeof(concat), "%s%s", secret, chal); SHA256((unsigned char *)concat, len, (unsigned char *)hash); return lws_b64_encode_string(hash, 32, out, 128);
}

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
	if (reason != LWS_CALLBACK_CLIENT_RECEIVE) {
		switch (reason) {
			case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
				fprintf(stderr, "failed to connect to obs\n");
				run = false;
				break;
			case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
				fprintf(stderr, "incorrect password\n");
				run = false;
			default:
				break;
		}
		return 0;
	}

	char *raw = strndup((char *)in, len);
	struct json_object *jr = json_tokener_parse(raw);
	struct json_object *jop, *jd, *ja, *jb, *jc;

	json_object_object_get_ex(jr, "op", &jop);
	json_object_object_get_ex(jr, "d", &jd);
	int op = json_object_get_int(jop);

	char authstr[64];
	char authjson[512] = { 0 };
	char buf[LWS_PRE + 1024] = { 0 };
	const char *salt, *chal;

	size_t printlen;

	switch (op) {
	case 0: // hello
		if (json_object_object_get_ex(jd, "authentication", &ja)) {
			if (passwd == NULL) {
				fprintf(stderr, "server requires a password\n");
				run = false;
				break;
			}
			json_object_object_get_ex(ja, "salt", &jb);
			salt = json_object_get_string(jb);
			json_object_object_get_ex(ja, "challenge", &jc);
			chal = json_object_get_string(jc);

			compute_auth(passwd, salt, chal, authstr);
			printlen = snprintf(authjson, sizeof(authjson), ",\"authentication\":\"%s\"", authstr);
		}
		printlen = snprintf(buf + LWS_PRE, sizeof(buf), "{\"op\":1,\"d\":{\"rpcVersion\":1%s}}", authjson);
		lws_write(wsi, (unsigned char *)buf + LWS_PRE, printlen, LWS_WRITE_TEXT);
		break;
	case 2:
		printlen = snprintf(buf + LWS_PRE, sizeof(buf), "{\"op\":6,\"d\":{\"requestId\":0,\"requestType\":\"%s\"%s}}", req[0], req[1] ? req[1] : "");
		lws_write(wsi, (unsigned char *)buf + LWS_PRE, printlen, LWS_WRITE_TEXT);
		break;
	case 7:
		run = false;
		json_object_object_get_ex(jd, "requestStatus", &ja);
		json_object_object_get_ex(ja, "code", &jb);
		if (json_object_get_int(jb) != 100) {
			fprintf(stderr, "request failed\n");
			break;
		}
		json_object_object_get_ex(jd, "responseData", &ja);
		const char *form = json_object_to_json_string_ext(ja, JSON_C_TO_STRING_PRETTY);
		printf("%s\n", form);
		ret = 0;
		break;
	}

	json_object_put(jr);
	free(raw);
	return 0;
}

static const struct lws_protocols protocols[] = {
	{ "obs", ws_callback },
	{ 0 }
};

int main(int argc, char **argv) {
	server = getenv("OBSCTL_SERVER");
	passwd = getenv("OBSCTL_PASSWD");

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

	req = &argv[optind];

	if (!server) server = "ws://localhost:4455";
	if (strncmp(server, "ws://", 5) != 0 || strchr(server+5, ':') == NULL) {
		fprintf(stderr, "server must be a valid websocket address\n");
		return 1;
	}

	char *host = strdup(server + 5);
	char *col = strchr(host, ':');
	*col = 0;
	int port = atoi(col + 1);

	struct lws_context_creation_info ctxi = { 0 };
	struct lws_client_connect_info i = { 0 };
	struct lws_context *ctx;

	lws_set_log_level(0, NULL);

	ctxi.port = CONTEXT_PORT_NO_LISTEN;
	ctxi.protocols = protocols;
	ctx = lws_create_context(&ctxi);

	i.context = ctx;
	i.host = host;
	i.address = host;
	i.origin = host;
	i.port = port;
	i.path = "/";
	i.protocol = protocols[0].name;

	lws_client_connect_via_info(&i);

	while (run) {
		lws_service(ctx, 0);
	}

	lws_context_destroy(ctx);
}
