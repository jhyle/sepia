#include <stdio.h>
#include "sepia.h"

void root(struct sepia_request * request)
{
	sepia_print_request(request);
}

int main(char ** args)
{
	sepia_init();
	sepia_mount("GET", "/test", &root);
	sepia_mount("POST", "/test2/{var}", &root);
	sepia_mount("GET", "/test3/{var}/test", &root);

	int result = sepia_start("127.0.0.1", 8888);

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
