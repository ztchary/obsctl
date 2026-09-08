#include <libwebsockets.h>
#include <json-c/json.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>
#include "commands.h"

int run = 1;
int ret = 1;
char *server;
char *passwd;
char **req;
struct json_object *request;

// ripped from clibs (because this is better than c23)
char *strndup(const char *s, size_t n)
{
    char* new = malloc(n+1);
    if (new) {
        strncpy(new, s, n);
        new[n] = '\0';
    }
    return new;
}

int compute_auth(const char *pass, const char *salt, const char *chal, char *out) {
	char concat[256];
	char hash[32] = {0};
	char secret[64];
	int len;

	len = snprintf(concat, sizeof(concat), "%s%s", pass, salt);
	SHA256((unsigned char *)concat, len, (unsigned char *)hash);
	lws_b64_encode_string(hash, 32, secret, sizeof(secret));

	len = snprintf(concat, sizeof(concat), "%s%s", secret, chal);
	SHA256((unsigned char *)concat, len, (unsigned char *)hash);
	return lws_b64_encode_string(hash, 32, out, 128);
}

static int ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
	if (reason != LWS_CALLBACK_CLIENT_RECEIVE) {
		switch (reason) {
			case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
				fprintf(stderr, "failed to connect to obs\n");
				run = 0;
				break;
			case LWS_CALLBACK_WS_PEER_INITIATED_CLOSE:
				fprintf(stderr, "incorrect password\n");
				run = 0;
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
				run = 0;
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
		printlen = snprintf(buf + LWS_PRE, sizeof(buf), "%s", json_object_to_json_string(request));
		lws_write(wsi, (unsigned char *)buf + LWS_PRE, printlen, LWS_WRITE_TEXT);
		break;
	case 7:
		run = 0;
		json_object_object_get_ex(jd, "requestStatus", &ja);
		json_object_object_get_ex(ja, "code", &jb);
		if (json_object_get_int(jb) != 100) {
			json_object_object_get_ex(ja, "comment", &jb);
			fprintf(stderr, "request failed\n%s\n", json_object_get_string(jb));
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

const struct Command *get_subcmd(const struct Command *root, const char *substr) {
	for (int i = 0; i < root->nsub; i++) {
		if (strcmp(root->sub[i]->cmd, substr) == 0) return root->sub[i];
	}
	return NULL;
}

void print_usage(int argc, char **argv, FILE *out) {
	char fullcmd[512];
	const struct Command *cmd = &obsctl_cmd;
	sprintf(fullcmd, "obsctl [flags]");
	for (int i = 0; i < argc; i++) {
		const struct Command *sub = get_subcmd(cmd, argv[i]);
		if (sub == NULL) break;
		sprintf(fullcmd + strlen(fullcmd), " %s", sub->cmd);
		cmd = sub;
	}

	if (cmd->type) {
		for (int i = 0; i < cmd->nparam; i++) {
			const struct Param *param = &cmd->param[i];
			sprintf(fullcmd + strlen(fullcmd), param->opt ? " [%s]" : " <%s>", param->name);
		}
		fprintf(out, "Usage: %s\n\n%s\n", fullcmd, cmd->desc);
		if (cmd->nparam) {
			fprintf(out, "\nParams:\n");
			for (int i = 0; i < cmd->nparam; i++) {
				const struct Param *param = &cmd->param[i];
				fprintf(out, "  %s (%s): %s\n", param->name, param_type_strs[param->type], param->desc);
			}
		}
		if (cmd->nresp) {
			fprintf(out, "\nResponse:\n");
			for (int i = 0; i < cmd->nresp; i++) {
				const struct Param *resp = &cmd->resp[i];
				fprintf(out, "  %s (%s%s): %s\n", resp->name, param_type_strs[resp->type], resp->opt ? "" : ", required", resp->desc);
			}
		}
		return;
	}
	fprintf(out, "Usage: %s <subcommand>\n\n", fullcmd);
	if (cmd->desc) fprintf(out, "%s\n\n", cmd->desc);
	fprintf(out, "Subcommands:\n");

	for (int i = 0; i < cmd->nsub; i++) {
		const struct Command *sub = cmd->sub[i];
		int len = sub->desc ? strcspn(sub->desc, ".") : 0;
		if (sub->desc) fprintf(out, "  %-20s : %.*s.\n", sub->cmd, len, sub->desc);
		else fprintf(out, "  %s ...\n", sub->cmd);
	}
}

int set_field_typed(struct json_object *data, const char *field, const char *value, enum ParamType type) {
	long nval;
	char *end;
	struct json_object *jr;
	switch (type) {
		case STRING:
			json_object_object_add(data, field, json_object_new_string(value));
			break;
		case NUMBER:
			nval = strtol(value, &end, 10);
			if (*end != 0) {
				fprintf(stderr, "parameter '%s' expects NUMBER, got '%s'\n", field, value);
				return -1;
			}
			json_object_object_add(data, field, json_object_new_int(nval));
			break;
		case OBJECT: case ANY:
			jr = json_tokener_parse(value);
			if (!jr) {
				fprintf(stderr, "parameter '%s' expects OBJECT, got '%s'\n", field, value);
				return -1;
			}
			json_object_object_add(data, field, jr);
			break;
		case BOOLEAN:
			nval = strchr("ty1", value[0]) != NULL;
			json_object_object_add(data, field, json_object_new_boolean(nval));
			break;
		default:
			assert(0 && "unreachable");
	}
	return 0;
}

int parse_params(const struct Command *cmd, int argc, char **argv, struct json_object *data) {
	int i;
	for (i = 0; i < cmd->nparam; i++) {
		if (cmd->param[i].opt) break;
		if (argc == 0) return -1;
		argc--;
		if (set_field_typed(data, cmd->param[i].name, *(argv++), cmd->param[i].type) != 0) return -1;
	}
	int start_opt = i;
	while (argc > 0) {
		if (i >= cmd->nparam) return -1;
		const char *eq = strchr(*argv, '=');
		if (!eq) {
			argc--;
		if (set_field_typed(data, cmd->param[i].name, *(argv++), cmd->param[i].type) != 0) return -1;
			continue;
		}
		char *pname = strndup(*argv, eq++ - *argv);
		int success = 0;
		for (int j = start_opt; j < cmd->nparam; j++) {
			if (strcmp(cmd->param[j].name, pname) != 0) continue;
			argc--; argv++; i++;
			if (set_field_typed(data, pname, eq, cmd->param[j].type) != 0) return -1;
			success = 1;
			break;
		}
		free(pname);
		if (!success) return -1;
	}
	return 0;
}

int main(int argc, char **argv) {
	server = getenv("OBSCTL_SERVER");
	passwd = getenv("OBSCTL_PASSWD");

	int opt;
	int optindex;
	int help = 0;

	const char *short_options = "s:p:h";
	struct option long_options[] = {
		{ "server", required_argument, NULL, 's' },
		{ "passwd", required_argument, NULL, 'p' },
		{ "help",   no_argument,       NULL, 'h' },
	};

	while ((opt = getopt_long(argc, argv, short_options, long_options, &optindex)) != -1) {
		switch (opt) {
			case 's': server = optarg; break;
			case 'p': passwd = optarg; break;
			case 'h': help = 1; break;
			default: break;
		}
	}

	if (help) {
		print_usage(argc - optind, &argv[optind], stdout);
		return 0;
	}

	if (optind == argc) {
		print_usage(0, NULL, stderr);
		return 1;
	}

	const struct Command *cmd = &obsctl_cmd;
	int start_param = 0;
	for (start_param = optind; start_param < argc; start_param++) {
		cmd = get_subcmd(cmd, argv[start_param]);
		if (cmd == NULL) {
			print_usage(argc - optind, &argv[optind], stderr);
			return 1;
		}
		if (cmd->type) {
			start_param++;
			break;
		}
	}

	if (!cmd->type) {
		print_usage(argc - optind, &argv[optind], stderr);
		return 1;
	}

	if (!server) server = "ws://localhost:4455";
	if (strncmp(server, "ws://", 5) != 0 || strchr(server+5, ':') == NULL) {
		fprintf(stderr, "server must be a valid websocket address\n");
		return 1;
	}

	struct json_object *data = json_object_new_object();
	if (parse_params(cmd, argc - start_param, &argv[start_param], data) != 0) {
		print_usage(argc - optind, &argv[optind], stderr);
		return 1;
	}

	struct json_object *op = json_object_new_int(6);
	struct json_object *d = json_object_new_object();

	json_object_object_add(d, "requestType", json_object_new_string(cmd->type));
	json_object_object_add(d, "requestId", json_object_new_int(0));
	json_object_object_add(d, "requestData", data);

	request = json_object_new_object();
	json_object_object_add(request, "op", op);
	json_object_object_add(request, "d", d);

	char *host = strndup(server + 5, strlen(server));
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
	json_object_put(request);

	return ret;
}
