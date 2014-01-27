#include <sepia.h>
#include <stdio.h>
#include <bstrlib.h>
#include <libbson-1.0/bson.h>

void root(struct sepia_request * request)
{
	sepia_print_request(request);
	sepia_send_string(request, sepia_request_var(request, 0));
	sepia_send_string(request, sepia_request_var(request, 1));

	int error;
	bson_t * input = sepia_read_json(request, &error);
	if (input == NULL) {
		sepia_send_string(request, bformat("Could not parse JSON body, error %d!\n", error));
	} else {
		sepia_send_json(request, input);
	}
}

int main(char ** args)
{
	sepia_init();
	sepia_mount("GET", "/test", &root);
	sepia_mount("POST", "/test2/{var}", &root);
	sepia_mount("GET", "/test3/{var}/test/{var}", &root);

	int result = sepia_start(NULL, 8888);

	if (result == SEPIA_ERROR_SOCKET) {
		printf("Failed to create socket!\n");
	}

	if (result == SEPIA_ERROR_BIND) {
		printf("Port 127.0.0.1:8888 already in use!\n");
	}

	if (result == SEPIA_ERROR_LISTEN) {
		printf("Failed to listen on socket!\n");
	}
}
