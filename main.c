#include <libwebsockets.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "usage.h"

bool run = true;
int ret = 1;
char *passwd;

int sha256sum(const char *input, size_t input_len, char *output) {
    struct lws_genhash_ctx ctx;
    
    if (lws_genhash_init(&ctx, LWS_GENHASH_TYPE_SHA256)) return -1;

    if (lws_genhash_update(&ctx, input, input_len)) {
        lws_genhash_destroy(&ctx, NULL);
        return -1;
    }

    return lws_genhash_destroy(&ctx, output);
}

int compute_auth(const char *pass, const char *salt, const char *chal, char *out) {
	char concat[256];
	char hash[32];
	char secret[64];
	int len;

	len = snprintf(concat, sizeof(concat), "%s%s", pass, salt);
	sha256sum(concat, len, hash);
	lws_b64_encode_string(hash, 32, secret, sizeof(secret));

	len = snprintf(concat, sizeof(concat), "%s%s", secret, chal);
	sha256sum(concat, len, hash);
	return lws_b64_encode_string(hash, 32, out, 128);
}

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
	printf("%d\n", reason);
	if (reason != LWS_CALLBACK_CLIENT_RECEIVE) {
		switch (reason) {
			case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
				fprintf(stderr, "failed to connect to obs\n");
				run = false;
				break;
			default:
				break;
		}
		return 0;
	}

	printf("(((%s)))\n", (char *)in);	

	struct json_object *jr = json_tokener_parse((char *)in);
	struct json_object *jop, *jd, *jauth, *jsalt, *jchal;

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
		if (json_object_object_get_ex(jd, "authentication", &jauth)) {
			if (passwd == NULL) {
				fprintf(stderr, "server requires a password\n");
				run = false;
				break;
			}
			json_object_object_get_ex(jr, "salt", &jsalt);
			salt = json_object_get_string(jsalt);
			json_object_object_get_ex(jr, "challenge", &jchal);
			chal = json_object_get_string(jchal);

			compute_auth(passwd, salt, chal, authstr);
			printlen = snprintf(authjson, sizeof(authjson), ",\"authentication\":\"%s\"", authstr);
		}
		snprintf(buf + LWS_PRE, sizeof(buf), "{\"op\":1,\"d\":{\"rpcVersion\":1%s}}", authjson);
		lws_write(wsi, (unsigned char *)buf + LWS_PRE, printlen, LWS_WRITE_TEXT);
		break;
	case 2: // auth success
	}

	json_object_put(jr);
	return 0;
}

static const struct lws_protocols protocols[] = {
	{ "obs", ws_callback },
	{ 0 }
};

int main(int argc, char **argv) {
	char *server = getenv("OBS_SERVER");
	passwd = getenv("OBS_PASSWD");

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
