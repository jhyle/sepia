#include <stdio.h>
#include <stdlib.h>
#include <libbson-1.0/bson.h>

#include "jsonsl.h"
#include "bstrlib.h"

#define MAX_NESTING_LEVEL 1024

struct bson_entry {
	bstring key;
	bson_t * bson;
	size_t array_index;
};

struct bson_state {
	char * text;
	int cur_entry;
	struct bson_entry entry[MAX_NESTING_LEVEL];
};

#define UTF8_2_BYTE_LIMIT 0x07FF

#define UTF8_BYTE_1_OF_2(x) (0b11000000 | ((x >> 6) & 0b00011111))
#define UTF8_BYTE_2_OF_2(x) (0b10000000 | (x & 0b00111111))

#define UTF8_BYTE_1_OF_3(x) (0b11100000 | ((x >> 12) & 0b00001111))
#define UTF8_BYTE_2_OF_3(x) (0b10000000 | ((x >> 6) & 0b00111111))
#define UTF8_BYTE_3_OF_3(x) (0b10000000 | (x & 0b00111111))

static bstring read_string(char * src, size_t length)
{
	int utf8_char;
	char hex_num[7] = "0x0000";

	char * end = src + length;
	bstring s = bfromcstralloc(length, "");
	char * dst = bdata(s);

	if (* src == '"') src++;

	while (src < end) {

		if (* src != '\\') {
			* dst = * src;
			dst++;

		} else {
			src++;
			if (* src == '"') {
				* dst = '"';
				dst++;
			} else if (* src == '\\') {
				* dst = '\\';
				dst++;
			} else if (* src == '/') {
				* dst = '/';
				dst++;
			} else if (* src == 'b') {
				* dst = '\b';
				dst++;
			} else if (* src == 'f') {
				* dst = '\f';
				dst++;
			} else if (* src == 'n') {
				* dst = '\n';
				dst++;
			} else if (* src == 'r') {
				* dst = '\r';
				dst++;
			} else if (* src == 't') {
				* dst = '\t';
				dst++;
			} else if (* src == 'u') {
				memcpy(hex_num + 2, src + 1, 4);
				sscanf(hex_num, "%x", &utf8_char);
				if (utf8_char <= UTF8_2_BYTE_LIMIT) {
					* dst = UTF8_BYTE_1_OF_2(utf8_char);
					dst++;
					* dst = UTF8_BYTE_2_OF_2(utf8_char);
					dst++;
				} else {
					* dst = UTF8_BYTE_1_OF_3(utf8_char);
					dst++;
					* dst = UTF8_BYTE_2_OF_3(utf8_char);
					dst++;
					* dst = UTF8_BYTE_3_OF_3(utf8_char);
					dst++;
				}
				src += 4;
			}
		} 
		src++;
	}	

	s->slen = dst - bdata(s);
	return s;
}

static void on_stack_change(jsonsl_t parser, jsonsl_action_t action, struct jsonsl_state_st * state, const jsonsl_char_t * at)
{
/*	printf("%c%c", action, state->type);
	if (state->type == JSONSL_T_SPECIAL) {
		printf("%c", state->special_flags);
	}
	printf("%d", state->pos_begin);
	if (action == JSONSL_ACTION_POP) {
		printf("-%d", state->pos_cur);
	}
	printf("\n");
*/
	struct bson_state * bstate = (struct bson_state *) parser->data;

	if (action == JSONSL_ACTION_PUSH) {

		if (state->type == JSONSL_T_OBJECT || state->type == JSONSL_T_LIST) {

			if (bstate->cur_entry > -1) {
				bstate->cur_entry++;
				bstate->entry[bstate->cur_entry].key = (bstring) state->data;
			} else {
				bstate->cur_entry = 0;
				bstate->entry[bstate->cur_entry].key = NULL;
			}

			bstate->entry[bstate->cur_entry].bson = bson_new();
			bstate->entry[bstate->cur_entry].array_index = 0;
		}

	} else if (action == JSONSL_ACTION_POP) {

		if (state->type == JSONSL_T_HKEY) {

			state->data = read_string(bstate->text + state->pos_begin, state->pos_cur - state->pos_begin);

		} else if (state->type == JSONSL_T_OBJECT || state->type == JSONSL_T_LIST) {

			if (bstate->cur_entry > 0) {
				struct bson_entry * entry = &(bstate->entry[bstate->cur_entry]);
				struct bson_entry * parent = &(bstate->entry[bstate->cur_entry - 1]);
				if (state->type == JSONSL_T_OBJECT) {
					bson_append_document(parent->bson, bdata(entry->key), blength(entry->key), entry->bson);
				} else {
					bson_append_array(parent->bson, bdata(entry->key), blength(entry->key), entry->bson);
				}
			}

			bstate->cur_entry--;

		} else {

			bstring key = (bstring) state->data;
			if (key == NULL) {
				key = bformat("%d", bstate->entry[bstate->cur_entry].array_index);
				bstate->entry[bstate->cur_entry].array_index++;
			}

			if (state->type == JSONSL_T_SPECIAL) {

				if (state->special_flags & JSONSL_SPECIALf_BOOLEAN) {
					bson_append_bool(bstate->entry[bstate->cur_entry].bson, bdata(key), blength(key), state->special_flags & JSONSL_SPECIALf_TRUE);

				} else if (state->special_flags & JSONSL_SPECIALf_NULL) {
					bson_append_null(bstate->entry[bstate->cur_entry].bson, bdata(key), blength(key));

				} else if (state->special_flags & JSONSL_SPECIALf_NUMERIC) {
					size_t length = state->pos_cur - state->pos_begin;
					char num[length + 1];
					* (num + length) = '\0';
					memcpy(num, bstate->text + state->pos_begin, length);

					if (state->special_flags & JSONSL_SPECIALf_NUMNOINT) {
						bson_append_double(bstate->entry[bstate->cur_entry].bson, bdata(key), blength(key), atof(num));
					} else {
						bson_append_int64(bstate->entry[bstate->cur_entry].bson, bdata(key), blength(key), atoi(num));
					}
				}

			} else {
				bstring value = read_string(bstate->text + state->pos_begin, state->pos_cur - state->pos_begin);
				bson_append_utf8(bstate->entry[bstate->cur_entry].bson, bdata(key), blength(key), bdata(value), blength(value));
			}
		}
	}
}

static int on_error(jsonsl_t jsn, jsonsl_error_t error, struct jsonsl_state_st * state, jsonsl_char_t * at)
{
	printf("Error %d!\n", error);
	return 0;
}

static struct bson_state bson_state_new(char * text)
{
	struct bson_state state;
	state.cur_entry = -1;
	state.text = text;
	return state;
}

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

	struct bson_state state = bson_state_new(buffer);

	jsonsl_t parser = jsonsl_new(MAX_NESTING_LEVEL);
	jsonsl_enable_all_callbacks(parser);
	parser->action_callback = on_stack_change;
	parser->error_callback = on_error;
	parser->data = &state;

	while ((read = fread(buffer, 1, 1024, file)) > 0) {
		jsonsl_feed(parser, buffer, read);
		if (read != 1024) {
			break;
		}
	}

	printf("%s\n", bson_as_json(state.entry[0].bson, NULL));

	jsonsl_destroy(parser);
	fclose(file);
}
