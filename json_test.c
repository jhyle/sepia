#include "json2bson.h"

int main(int argc, char ** args)
{
	if (argc < 2) {
		printf("Usage: json_test test.json\n");
		return 1;
	}

	int read;
	char buffer[1024];
	FILE * file = fopen(args[1], "r");

	if (file == NULL) {
		printf("File %s not found!\n", args[1]);
		return 2;
	}

	read = fread(buffer, 1, 1024, file);
	struct sepia_request request;
	request.body = blk2bstr(buffer, read);
	printf("%s\n", bson_as_json(sepia_read_json(&request), NULL));

	fclose(file);
}
