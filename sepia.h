#include <gc.h>
#include <stdarg.h>
#include <syslog.h>
#include <bstrlib.h>
#include <libbson-1.0/bson.h>

#define SEPIA_OK 0

/*
  The error return values of sepia_init().
*/
#define SEPIA_ERROR_SOCKET 1
#define SEPIA_ERROR_BIND   2
#define SEPIA_ERROR_LISTEN 3

/*
  The error return value of sepia_send_status().
*/
#define SEPIA_ERROR_STATUS_ALREADY_SEND  4
/*
	The error return value of sepia_send_header().
*/
#define SEPIA_ERROR_HEADERS_ALREADY_SEND 5

/*
  All states of sepia_request.status.
*/
#define SEPIA_REQUEST_INIT 10
#define SEPIA_REQUEST_READ 11
#define SEPIA_REQUEST_STATUS_SEND  12
#define SEPIA_REQUEST_HEADERS_SEND 13

/*
  Simple log method that wraps syslog(). The full signature is
  sepia_log(int level, char * fmt, ...). Possible values for level are:
  LOG_EMERG, LOG_ALERT, LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_NOTICE,
  LOG_INFO and LOG_DEBUG. See syslog.h.
*/
#define sepia_log(level, ...) syslog(level, __VA_ARGS__)

struct sepia_mount;
struct sepia_request;

/*
  Initialize. Actually this wraps GC_INIT(), so you should call it before anything.
*/
void sepia_init();

/*
  Connect a HTTP method / path (-prefix) with a request handler.
  The path can contain path variables which actual values are captured.
  Path variables must start after a slash and end before a slash or the
  end of the path. The name between { and } is not used and can be omitted.

  Example: sepia_mount('GET', '/myApp/doSomething/{ID}', &doSomething);

  The will call doSomething for GET-requests to /myApp/doSomething/123 but also
  /myApp/doSomething/123/and/456. In each case the first and only path var will
  have the string value "123".

  Please note that the first matching mount is taken. Also all mounts have to
  be set before calling sepia_start(). If no mount matches a request, a 404
  response is send to the client.
*/
void sepia_mount(char * method, char * path, void (* handler)(struct sepia_request *));

/*
  Start a server that listens on the given ip and port.
  Path NULL for ip to listen on all interfaces.
*/
int  sepia_start(char * ip, int port);

/*
  Retrieve the value of the n'th path var. Return NULL if not exists.
*/
bstring sepia_request_var(struct sepia_request *, size_t n);

/*
  Retrieve the value of a request attribute by name. See sepia_print_request().
*/
bstring sepia_request_attribute(struct sepia_request *, bstring name);

/*
  Read the body of an HTTP request as string.
*/
bstring sepia_read_body(struct sepia_request *);

/*
  Read the body of the HTTP request as JSON. Returns NULL if unsuccessful.
*/
bson_t * sepia_read_json(struct sepia_request *);

/*
  Send an HTTP status. The status includes the number and the describing string.
  You can call this method only ones, it is omitable if the status is "200 OK".
*/
int  sepia_send_status(struct sepia_request *, bstring status);

/*
  Send an HTTP header. Not possible after sepia_send_eohs(), _data, _string and _json.
*/
int  sepia_send_header(struct sepia_request *, bstring name, bstring value);

/*
  Send end-of-headers signature (actuall \r\n). You cannot send a header after this.
  Will be executed automatically before sepia_send_data(), _string() or _json().
*/
void sepia_send_eohs  (struct sepia_request *);

/*
  Send raw body data described by pointer and size.
*/
void sepia_send_data  (struct sepia_request *, void *, size_t);

/*
  Send string data.
*/
void sepia_send_string(struct sepia_request *, bstring);

/*
  Send JSON.
*/
void sepia_send_json  (struct sepia_request *, bson_t * b);

/*
  Send all request and path vars as JSON object.
*/
void sepia_print_request(struct sepia_request *);
