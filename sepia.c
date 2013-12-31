#include <gc.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "sepia.h"
#include "netstring.c"

int sepia_send_status(struct sepia_request * request, char * status, size_t status_len)
{
	if (request->status != SEPIA_REQUEST_READ) {
		return SEPIA_ERROR_STATUS_ALREADY_SEND;
	}

	send(request->socket, "Status: ", 8, 0);
	send(request->socket, status, status_len, 0);
	send(request->socket, "\r\n", 2, 0);

	request->status = SEPIA_REQUEST_STATUS_SEND;
	return SEPIA_OK;
}

int sepia_send_header(struct sepia_request * request, char * key, size_t key_len, char * value, size_t value_len)
{
	if (request->status == SEPIA_REQUEST_READ) {
		sepia_send_status(request, "200 OK", 6); 
	}

	if (request->status == SEPIA_REQUEST_HEADERS_SEND) {
		return SEPIA_ERROR_HEADERS_ALREADY_SEND;
	}

	send(request->socket, key, key_len, 0);
	send(request->socket, ": ", 2, 0);
	send(request->socket, value, value_len, 0);
	send(request->socket, "\r\n", 2, 0);

	return SEPIA_OK;
}

void sepia_send_eohs(struct sepia_request * request)
{
	if (request->status == SEPIA_REQUEST_READ) {
		sepia_send_status(request, "200 OK", 6);
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

char * sepia_request_attribute(struct sepia_request * request, char * name)
{
	size_t i;

	for (i = 0; i < request->header_count; i++) {
		if (strcmp(request->header_keys[i], name) == 0) {
			return request->header_values[i];
		}
	}

	return NULL;
}

static void * clone_buffer(void * buffer, size_t buffer_len)
{
	void * clone = GC_MALLOC(buffer_len);

	if (clone != NULL) {
		memcpy(clone, buffer, buffer_len);
	}

	return clone;
}

static void split_by_char(void * buffer, size_t buffer_len, char c, char *** values, size_t * value_count)
{
	char * string = clone_buffer(buffer, buffer_len + 1);

	size_t count = 1;
	char * pos = string;
	* (pos + buffer_len) = '\0';

	while ((pos = memchr(pos, c, buffer_len - (pos - string))) != NULL) {
		count++;
		pos++;
	}
	
	* value_count = count;
	* values = GC_MALLOC(count * sizeof(char *));

	char * next;
	size_t i = 0;
	pos = string;

	do {
		(* values)[i] = pos;
		next = memchr(pos, c, buffer_len - (pos - string));
		if (next != NULL) {
			* next = '\0';
			pos = next + 1;
			i++;
		}
	} while (next != NULL);
}

void sepia_print_request(struct sepia_request * request)
{
	size_t i;

	if (request->status != SEPIA_REQUEST_HEADERS_SEND) {
		sepia_send_header(request, "Content-Type", 12, "text/plain", 10);
	}

	for (i = 0; i < request->header_count; i++) {
		sepia_send_data(request, request->header_keys[i], strlen(request->header_keys[i]));
		sepia_send_data(request, ": ", 2);
		sepia_send_data(request, request->header_values[i], strlen(request->header_values[i]));
		sepia_send_data(request, "\n", 1);
	}

	sepia_send_data(request, request->body, strlen(request->body));
}

void sepia_init()
{
	GC_INIT();
}

static struct sepia_mount {
	size_t part_count;
	char ** parts;
	void (* handler)(struct sepia_request *);
} * mounts = NULL;

static size_t mount_count = 0;

// not thread safe!
void sepia_mount(char * path, void (* handler)(struct sepia_request *))
{
	size_t n = mount_count, i;

	mount_count++;
	mounts = GC_REALLOC(mounts, mount_count * sizeof(struct sepia_mount));

	split_by_char(path, strlen(path), '/', &(mounts[n].parts), &(mounts[n].part_count));
	mounts[n].handler = handler;
}

#define find_next_null(str, cur_pos, str_length) memchr(cur_pos, '\0', str_length - (cur_pos - str))

static void parse_request_headers(char * s, size_t l, struct sepia_request * req)
{
	char * p = s;
	size_t null_count = 0;

	while ((p = find_next_null(s, p, l)) != NULL) {
		null_count++;
		p++;
	}

	req->header_count = null_count >> 1;
	req->header_keys = GC_MALLOC(req->header_count * sizeof(char *));
	req->header_values = GC_MALLOC(req->header_count * sizeof(char *));

	p = s - 1;
	size_t i = 0;

	do {
		p++;
		req->header_keys[i] = p;
		p = find_next_null(s, p, l);
		if (p != NULL) p++;
		req->header_values[i] = p;
		i++;
	} while (p != NULL && (p = find_next_null(s, p, l)) != NULL);
}

#define BUFFER_SIZE 32

static struct sepia_request * read_request(int socket)
{
	char buffer[BUFFER_SIZE];
	int read = recv(socket, &buffer, BUFFER_SIZE, MSG_PEEK);
	char * eos = memchr(buffer, ':', read);

	if (eos != NULL) {
		struct sepia_request * req = GC_MALLOC(sizeof(struct sepia_request));
		req->status = SEPIA_REQUEST_INIT;
		req->socket = socket;
		* eos = '\0';
		req->buffer_size = atoi(buffer) + (eos - buffer) + 2;
		req->buffer = (char *) GC_MALLOC(req->buffer_size + 1);
		* (req->buffer + req->buffer_size) = '\0';

		if (recv(socket, req->buffer, req->buffer_size, MSG_WAITALL) == req->buffer_size) {
			char * net_start;
			size_t net_length;
			if (netstring_read(req->buffer, req->buffer_size, &net_start, &net_length) == 0) {
				parse_request_headers(net_start, net_length, req);
				req->body = net_start + net_length + 1;
				req->status = SEPIA_REQUEST_READ;
				return req;
			}
		}
	}

	return NULL;
}

static int path_matches(struct sepia_mount * mount, char ** path_parts, size_t path_part_count)
{
	if (mount->part_count > path_part_count) return 0;

	size_t i;
	for (i = 0; i < path_part_count && i < mount->part_count; i++) {
		if (* mount->parts[i] != '\0' && * mount->parts[i] != '{' && strcmp(path_parts[i], mount->parts[i]) != 0) return 0;
	}

	return 1;
}

void handle_request(struct sepia_request * request)
{
	char * path_info = sepia_request_attribute(request, "PATH_INFO");
	
	if (path_info != NULL) {
		char ** path_parts;
		size_t path_part_count, i;
		split_by_char(path_info, strlen(path_info), '/', &path_parts, &path_part_count);

		for (i = 0; i < mount_count; i++) {
			if (path_matches(&mounts[i], path_parts, path_part_count)) {
				mounts[i].handler(request);
				return;
			}
		}
	}

	sepia_send_status(request, "404 Not Found", 13);
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
				printf("Failed to read the request!\n");
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

