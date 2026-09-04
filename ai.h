#include <libwebsockets.h>
#include <json-c/json.h>
#include <string.h>
#include <stdio.h>

#define OBS_PASSWORD "your_password_here"
#define TARGET_SCENE "Scene 1"

static int interrupted = 0;
static struct lws *web_socket = NULL;

// Helper to compute OBS Authentication string
void compute_auth(const char *password, const char *salt, const char *challenge, char *out_auth) {
    unsigned char hash1[32];
    unsigned char hash2[32];
    char secret_b64[64];
    char concat[256];

    // 1. hash1 = SHA256(password + salt)
    snprintf(concat, sizeof(concat), "%s%s", password, salt);
    lws_sha256_checksum((unsigned char *)concat, strlen(concat), hash1);
    
    // 2. secret = Base64(hash1)
    lws_b64_encode_string((const char *)hash1, 32, secret_b64, sizeof(secret_b64));

    // 3. hash2 = SHA256(secret + challenge)
    snprintf(concat, sizeof(concat), "%s%s", secret_b64, challenge);
    lws_sha256_checksum((unsigned char *)concat, strlen(concat), hash2);

    // 4. auth_string = Base64(hash2)
    lws_b64_encode_string((const char *)hash2, 32, out_auth, 128);
}

static int callback_obs(struct lws *wsi, enum lws_callback_reasons reason,
                        void *user, void *in, size_t len) {
    switch (reason) {
        case LWS_CALLBACK_CLIENT_RECEIVE: {
            struct json_object *root = json_tokener_parse((char *)in);
            struct json_object *op_obj, *d_obj;
            
            json_object_object_get_ex(root, "op", &op_obj);
            json_object_object_get_ex(root, "d", &d_obj);
            int op = json_object_get_int(op_obj);

            // OP 0: Hello (Initial server message)
            if (op == 0) {
                struct json_object *auth_obj, *salt_obj, *challenge_obj;
                json_object_object_get_ex(d_obj, "authentication", &auth_obj);
                
                char auth_resp[128];
                json_object_object_get_ex(auth_obj, "salt", &salt_obj);
                json_object_object_get_ex(auth_obj, "challenge", &challenge_obj);

                compute_auth(OBS_PASSWORD, 
                             json_object_get_string(salt_obj), 
                             json_object_get_string(challenge_obj), 
                             auth_resp);

                // Send OP 1: Identify
                char identify_msg[512];
                snprintf(identify_msg, sizeof(identify_msg),
                    "{\"op\":1,\"d\":{\"rpcVersion\":1,\"authentication\":\"%s\"}}", auth_resp);
                
                unsigned char buf[LWS_PRE + 512];
                memcpy(&buf[LWS_PRE], identify_msg, strlen(identify_msg));
                lws_write(wsi, &buf[LWS_PRE], strlen(identify_msg), LWS_WRITE_TEXT);
            }
            // OP 2: Identified (Auth Success)
            else if (op == 2) {
                lwsl_user("Authenticated! Switching scene...\n");
                char req[512];
                snprintf(req, sizeof(req), 
                    "{\"op\":6,\"d\":{\"requestType\":\"SetCurrentProgramScene\",\"requestId\":\"1\",\"requestData\":{\"sceneName\":\"%s\"}}}", 
                    TARGET_SCENE);

                unsigned char buf[LWS_PRE + 512];
                memcpy(&buf[LWS_PRE], req, strlen(req));
                lws_write(wsi, &buf[LWS_PRE], strlen(req), LWS_WRITE_TEXT);
                interrupted = 1; // Exit after sending
            }

            json_object_put(root);
            break;
        }
        
        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
            lwsl_err("Connection Error\n");
            interrupted = 1;
            break;

        default:
            break;
    }
    return 0;
}

// ... (Rest of main() remains similar to the previous example) ...

static const struct lws_protocols protocols[] = {
    { "obs-protocol", callback_obs, 0, 0 },
    { NULL, NULL, 0, 0 }
};

int main() {
    struct lws_context_creation_info info;
    struct lws_client_connect_info i;
    struct lws_context *context;

    memset(&info, 0, sizeof info);
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    context = lws_create_context(&info);

    memset(&i, 0, sizeof i);
    i.context = context;
    i.address = "127.0.0.1";
    i.port = 4455;
    i.path = "/";
    i.host = i.address;
    i.origin = i.address;
    i.protocol = protocols[0].name;

    lws_client_connect_via_info(&i);

    while (!interrupted) {
        lws_service(context, 0);
    }

    lws_context_destroy(context);
    return 0;
}
