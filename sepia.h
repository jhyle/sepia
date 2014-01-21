#include <gc.h>
#include <stdarg.h>
#include <syslog.h>
#include "bstrlib.h"
#include <libbson-1.0/bson.h>

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

#define sepia_log(level, ...) syslog(level, __VA_ARGS__)

struct sepia_request;

struct sepia_mount {
	bstring method;
	struct bstrList * path;
	char * path_var;
	void (* handler)(struct sepia_request *);
}; 

struct sepia_request {
	int status;
	int socket;
	struct bstrList * headers;
	bstring body;
	struct bstrList * path;
	struct sepia_mount * mount;
};

void sepia_init();
void sepia_mount(char *, char *, void (* handler)(struct sepia_request *));
int  sepia_start(char *, int);

bstring sepia_request_var(struct sepia_request *, size_t);
bstring sepia_request_attribute(struct sepia_request *, bstring);

bson_t * sepia_read_json(struct sepia_request *);

int  sepia_send_status(struct sepia_request *, bstring);
int  sepia_send_header(struct sepia_request *, bstring, bstring);
void sepia_send_eohs  (struct sepia_request *);
void sepia_send_data  (struct sepia_request *, void *, size_t);
void sepia_send_string(struct sepia_request *, bstring);
void sepia_send_json  (struct sepia_request *, bson_t * b);

void sepia_print_request(struct sepia_request *);
