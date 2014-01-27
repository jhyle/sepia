Sepia
=====

Sepia is a small c lib that routes HTTP requests from a webserver to request handling methods. Furthermore it supports serialization and unserialization of JSON strings. See sepia.h for a documented list of all methods. Sepia is build on top of the following components:

* libgc - the Hans J. Boehm garbage collector
* libbstr - the Better String library from Paul Hsieh
* libbson - the Binary JSON library of the MongoDB project
* jsonsl - a small JSON parser: https://github.com/mnunberg/jsonsl

These components were choosen for some reason:

Using a garbage collector increases developer productivity by 30% at average. You should use one unless you have a reason not to.

String manipulation is the core of web programming. So it should be as convenient and fast as possible - and offer a lot more methods than the c string lib. Take a look into the bstring documentation to see what it has to offer.

JSON is the most used data exchange format for REST services. BSON offers the methods to walk through and manipulate the incoming and outgoing data without changing the data format itself. Also you can use MongoDB as a persistence store directly.

How to build
============

To build sepia you have to install libgc and make libbstr and libbson. Note that you must take the support libs from my repo as they are slightly modified to support libgc. The following steps are sufficient on an Ubuntu based system:

```
sudo apt-get install libgc-dev automake autoconf libtool make gcc git

git clone https://github.com/jhyle/bstrlib.git
cd bstrlib
autoreconf --install
./configure
make
sudo make install
cd ..

git clone https://github.com/jhyle/libbson.git
cd libbson
autoreconf --install
./configure --enable-libgc
make
sudo make install
cd ..

git clone https://github.com/jhyle/sepia.git
cd sepia
autoreconf --install
./configure
make
sudo make install
cd ..

sudo ldconfig
```

Configure Apache for SCGI
=========================

Sepia depends on a webserver to handle and forward HTTP requests. The following command will install Apache with the necessary module:

```
sudo apt-get install apache2 libapache2-mod-scgi
```

Then you have create the file /etc/apache2/conf-available/scgi.conf to describe the pathes to be forwarded to your SCGI server, e.g.:

```
SCGIMount / 127.0.0.1:8888
```

Which will forward every request (path = /) towards the example web server below.

Example
=======

See server.c for a small example how to use Sepia:

```
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
```

