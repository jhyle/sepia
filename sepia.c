#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "sepia.h"
#include "netstring.c"

struct tagbstring PATH_INFO = bsStatic("PATH_INFO");
struct tagbstring CONTENT_LENGTH = bsStatic("CONTENT_LENGTH");
struct tagbstring REQUEST_METHOD = bsStatic("REQUEST_METHOD");
struct tagbstring HTTP_STATUS_OK = bsStatic("200 OK");
struct tagbstring HTTP_STATUS_NOT_FOUND = bsStatic("404 Not Found");
struct tagbstring HTTP_HEADER_CONTENT_TYPE = bsStatic("Content-Type");
struct tagbstring HTTP_HEADER_CONTENT_TYPE_TEXT_PLAIN = bsStatic("text/plain");
struct tagbstring HTTP_HEADER_CONTENT_TYPE_APPLICATION_JSON = bsStatic("application/json");

void sepia_init()
{
	GC_INIT();
}

static size_t mount_count = 0;
static struct sepia_mount * mounts = NULL;

#define is_path_var(x) (blength(x) > 0 && * bdata(x) == '{')

// not thread safe!
void sepia_mount(char * method, char * path, void (* handler)(struct sepia_request *))
{
	size_t n = mount_count;

	mount_count++;
	mounts = GC_REALLOC(mounts, mount_count * sizeof(struct sepia_mount));

	mounts[n].handler = handler;
	mounts[n].method = bfromcstr(method);
	mounts[n].path = bsplit(bfromcstr(path), '/');
	mounts[n].path_var = GC_MALLOC(mounts[n].path->qty * sizeof(char));

	size_t i;
	for (i = 0; i < mounts[n].path->qty; i++) {
		mounts[n].path_var[i] = is_path_var(mounts[n].path->entry[i]);
	}
}

bstring sepia_request_attribute(struct sepia_request * request, bstring name)
{
	size_t i;

	for (i = 0; i < request->headers->qty; i += 2) {
		if (biseq(request->headers->entry[i], name)) {
			return request->headers->entry[i + 1];
		}
	}

	return NULL;
}

bstring sepia_request_var(struct sepia_request * request, size_t n)
{
	size_t i, j = 0;

	for (i = 0; i < request->mount->path->qty; i++) {
		if (request->mount->path_var[i]) {
			if (n == j) {
				return request->path->entry[i];
			} else {
				j++;
			}
		}
	}

	return NULL;
}


static int bstr2int(bstring b)
{
	if (b == NULL) return 0;
	
	size_t length = blength(b);
	char buffer[length + 1];
	buffer[length] = '\0';
	memcpy(buffer, b->data, length);
	return atoi(buffer);	
}

#define BUFFER_SIZE 32

static struct sepia_request * read_request(int socket)
{
	char buffer[BUFFER_SIZE];
	int read = recv(socket, &buffer, BUFFER_SIZE, MSG_PEEK);
	char * end_of_size = memchr(buffer, ':', read);

	if (end_of_size != NULL) {
		* end_of_size = '\0';
		int buffer_size = atoi(buffer) + (end_of_size - buffer) + 2;
		char * buffer = (char *) GC_MALLOC(buffer_size);

		if (recv(socket, buffer, buffer_size, MSG_WAITALL) == buffer_size) {

//			printf("%s\n", bstr2cstr(blk2bstr(buffer, buffer_size), '\n'));

			size_t netstr_length;
			char * netstr_start;

			if (netstring_read(buffer, buffer_size, &netstr_start, &netstr_length) == 0) {
				struct tagbstring netstr;
				blk2tbstr(netstr, netstr_start, netstr_length);

				struct sepia_request * req = GC_MALLOC(sizeof(struct sepia_request));
				req->status = SEPIA_REQUEST_READ;
				req->socket = socket;
				req->headers = bsplit(&netstr, '\0');
				req->headers->qty--;
				req->path = bsplit(sepia_request_attribute(req, &PATH_INFO), '/');

				int body_size = bstr2int(sepia_request_attribute(req, &CONTENT_LENGTH));
				if (body_size == 0) {
					req->body = NULL;
					return req;
				} else {
					req->body = GC_MALLOC(sizeof(bstring *));
					char * body_buffer = GC_MALLOC(body_size);
					if (recv(socket, body_buffer, body_size, MSG_WAITALL) == body_size) {
						btfromblk(* (req->body), body_buffer, body_size);
						return req;
					}
				}
			}
		}
	}

	return NULL;
}

int sepia_send_status(struct sepia_request * request, bstring s)
{
	if (request->status != SEPIA_REQUEST_READ) {
		return SEPIA_ERROR_STATUS_ALREADY_SEND;
	}

	send(request->socket, "Status: ", 8, 0);
	send(request->socket, bdata(s), blength(s), 0);
	send(request->socket, "\r\n", 2, 0);

	request->status = SEPIA_REQUEST_STATUS_SEND;
	return SEPIA_OK;
}

int sepia_send_header(struct sepia_request * request, bstring key, bstring value)
{
	if (request->status == SEPIA_REQUEST_READ) {
		sepia_send_status(request, &HTTP_STATUS_OK); 
	}

	if (request->status == SEPIA_REQUEST_HEADERS_SEND) {
		return SEPIA_ERROR_HEADERS_ALREADY_SEND;
	}

	send(request->socket, bdata(key), blength(key), 0);
	send(request->socket, ": ", 2, 0);
	send(request->socket, bdata(value), blength(value), 0);
	send(request->socket, "\r\n", 2, 0);

	return SEPIA_OK;
}

void sepia_send_eohs(struct sepia_request * request)
{
	if (request->status == SEPIA_REQUEST_READ) {
		sepia_send_status(request, &HTTP_STATUS_OK);
	}

	send(request->socket, "\r\n", 2, 0);

	request->status = SEPIA_REQUEST_HEADERS_SEND;
}

void sepia_send_data(struct sepia_request * request, void * data, size_t data_len)
{
	if (request->status != SEPIA_REQUEST_HEADERS_SEND) {
		sepia_send_eohs(request);
	}
	send(request->socket, data, data_len, 0);
}

void sepia_send_string(struct sepia_request * request, bstring s)
{
	if (request->status != SEPIA_REQUEST_HEADERS_SEND) {
		sepia_send_header(request, &HTTP_HEADER_CONTENT_TYPE, &HTTP_HEADER_CONTENT_TYPE_TEXT_PLAIN);
	}

	sepia_send_data(request, bdata(s), blength(s));
}

void sepia_send_json(struct sepia_request * request, bson_t * b)
{
	if (request->status != SEPIA_REQUEST_HEADERS_SEND) {
		sepia_send_header(request, &HTTP_HEADER_CONTENT_TYPE, &HTTP_HEADER_CONTENT_TYPE_APPLICATION_JSON);
	}

	if (b != NULL) {
		size_t len;
		char * json = bson_as_json(b, &len);
		sepia_send_data(request, json, len);
	}
}

void sepia_print_request(struct sepia_request * request)
{
	size_t i;

	bson_t * b = bson_new();

	for (i = 0; i < request->headers->qty; i+= 2) {
		bson_append_utf8(b,
			bdata(request->headers->entry[i]), blength(request->headers->entry[i]),
			bdata(request->headers->entry[i + 1]), blength(request->headers->entry[i + 1]));
	}

	sepia_send_json(request, b);
}

static int path_matches(struct bstrList * mount_path, struct bstrList * request_path)
{
	if (mount_path->qty > request_path->qty) return 0;

	size_t i;
	for (i = 0; i < mount_path->qty && i < request_path->qty; i++) {
		if (blength(mount_path->entry[i]) > 0) {
			if (* bdata(mount_path->entry[i]) == '{') {
				// nothing to do
			} else {
				if (!biseq(mount_path->entry[i], request_path->entry[i])) return 0;
			}
		}
	}

	return 1;
}

void handle_request(struct sepia_request * request)
{
	size_t i;

	for (i = 0; i < mount_count; i++) {
		if (biseq(sepia_request_attribute(request, &REQUEST_METHOD), mounts[i].method) && path_matches(mounts[i].path, request->path)) {
			request->mount = &mounts[i];
			mounts[i].handler(request);
			return;
		}
	}

	sepia_send_status(request, &HTTP_STATUS_NOT_FOUND);
	sepia_send_eohs(request);
}

// TODO ip == NULL -> listen on all interfaces
// TODO logfile
int sepia_start(char * ip, int port)
{
	int sock;
	struct sockaddr_in address;

	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 1) {
		return SEPIA_ERROR_SOCKET;
	}

	const int y = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &y, sizeof(int));

	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	inet_aton(ip, &address.sin_addr);
	
	if (bind(sock, (const struct sockaddr *) &address, sizeof(address)) != 0) {
		return SEPIA_ERROR_BIND;
	}

	if (listen(sock, 100) != 0) {
		return SEPIA_ERROR_LISTEN;
	}

	while (1) {
		int conn = accept(sock, NULL, 0);
		if (fork() == 0) {
			close(sock);
			struct sepia_request * req = read_request(conn);
			if (req == NULL) {
				sepia_log(LOG_ERR, "Could not read a request.");
			} else {
				handle_request(req);
			}
			close(conn);
			return SEPIA_OK;
		} else {
			close(conn);
		}
	}

	close(sock);
	return SEPIA_OK;
}

