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
	struct sepia_request * request = sepia_fake_request(buffer, read);
	printf("%s\n", bson_as_json(sepia_read_json(request, NULL), NULL));

	fclose(file);
}
