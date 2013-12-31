#include "bstrlib.h"

#define SEPIA_OK 0
#define SEPIA_ERROR_SOCKET 1
#define SEPIA_ERROR_BIND   2
#define SEPIA_ERROR_LISTEN 3
#define SEPIA_ERROR_STATUS_ALREADY_SEND  4
#define SEPIA_ERROR_HEADERS_ALREADY_SEND 5

#define SEPIA_REQUEST_INIT 10
#define SEPIA_REQUEST_READ 11
#define SEPIA_REQUEST_STATUS_SEND  12
#define SEPIA_REQUEST_HEADERS_SEND 13

struct sepia_request {
	int status;
	int socket;
	struct bstrList * headers;
	bstring body;
};

void sepia_init();
void sepia_mount(char *, void (* handler)(struct sepia_request *));
int  sepia_start(char *, int);

int  sepia_send_status(struct sepia_request *, bstring);
int  sepia_send_header(struct sepia_request *, bstring, bstring);
void sepia_send_eohs  (struct sepia_request *);
void sepia_send_data  (struct sepia_request *, void *, size_t);
void sepia_send_string(struct sepia_request *, bstring);

void sepia_print_request(struct sepia_request *);
