#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "bson2json.h"

struct tagbstring JSON_END = bsStatic(" }");
struct tagbstring JSON_START = bsStatic("{ ");

struct tagbstring CONTENT_TYPE = bsStatic("Content-Type");
struct tagbstring CONTENT_TYPE_APPLICATION_JSON = bsStatic("application/json");

struct bson_json_state
{
   bson_uint32_t  count;
   bson_bool_t    keys;
   bson_uint32_t  depth;
   struct sepia_request * request;
};

static inline void
_bson_utf8_get_sequence (const char   *utf8,
                         bson_uint8_t *seq_length)
{
   unsigned char c = *(const unsigned char *)utf8;
   bson_uint8_t n;

   /*
    * See the following[1] for a description of what the given multi-byte
    * sequences will be based on the bits set of the first byte. We also need
    * to mask the first byte based on that.  All subsequent bytes are masked
    * against 0x3F.
    *
    * [1] http://www.joelonsoftware.com/articles/Unicode.html
    */

   if ((c & 0x80) == 0) {
      n = 1;
   } else if ((c & 0xE0) == 0xC0) {
      n = 2;
   } else if ((c & 0xF0) == 0xE0) {
      n = 3;
   } else if ((c & 0xF8) == 0xF0) {
      n = 4;
   } else if ((c & 0xFC) == 0xF8) {
      n = 5;
   } else if ((c & 0xFE) == 0xFC) {
      n = 6;
   } else {
      n = 0;
   }

   *seq_length = n;
}

static void sepia_send_utf8(struct sepia_request * request, const char * str, int length)
{
	unsigned int i = 0;
	unsigned int o = 0;
	bson_uint8_t seq_len;

	if (str == NULL) return;

	if (length < 0) {
		length = strlen(str);
	}

	while (i < length) {
		_bson_utf8_get_sequence (&str[i], &seq_len);

		if ((i + seq_len) > length) {
			return;
		}

		switch (str[i]) {
		case '"':
		case '\\':
			sepia_send_data(request, "\\", 1);
			o++;
		/* fall through */
		default:
			sepia_send_data(request, &str[i], seq_len);
			o += seq_len;
			break;
		}

		i += seq_len;
	}
}

static bson_bool_t
_bson_as_json_visit_array (const bson_iter_t *iter,
                           const char        *key,
                           const bson_t      *v_array,
                           void              *data);

static bson_bool_t
_bson_as_json_visit_document (const bson_iter_t *iter,
                              const char        *key,
                              const bson_t      *v_document,
                              void              *data);

static bson_bool_t
_bson_as_json_visit_utf8 (const bson_iter_t *iter,
                          const char        *key,
                          size_t             v_utf8_len,
                          const char        *v_utf8,
                          void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;

	sepia_send_data(state->request, "\"", 1);
	sepia_send_utf8(state->request, v_utf8, v_utf8_len);
	sepia_send_data(state->request, "\"", 1);

	return FALSE;
}

#define BUFFER_SIZE 32

static bson_bool_t
_bson_as_json_visit_int32 (const bson_iter_t *iter,
                           const char        *key,
                           bson_int32_t       v_int32,
                           void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;
	char str[BUFFER_SIZE];

	int length = snprintf(str, sizeof str, "%" PRId32, v_int32);
	if (length >= BUFFER_SIZE) {
		length = BUFFER_SIZE - 1;
	}
	sepia_send_data(state->request, str, length);

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_int64 (const bson_iter_t *iter,
                           const char        *key,
                           bson_int64_t       v_int64,
                           void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;
	char str[BUFFER_SIZE];

	int length = snprintf(str, sizeof str, "%" PRIi64, v_int64);
	if (length >= BUFFER_SIZE) {
		length = BUFFER_SIZE - 1;
	}
	sepia_send_data(state->request, str, length);

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_double (const bson_iter_t *iter,
                            const char        *key,
                            double             v_double,
                            void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;
	char str[BUFFER_SIZE];

	int length = snprintf(str, sizeof str, "%lf", v_double);
	if (length >= BUFFER_SIZE) {
		length = BUFFER_SIZE - 1;
	}
	sepia_send_data(state->request, str, length);

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_undefined (const bson_iter_t *iter,
                               const char        *key,
                               void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;

	sepia_send_data(state->request, "{ \"$undefined\" : true }", 23);

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_null (const bson_iter_t *iter,
                          const char        *key,
                          void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;

	sepia_send_data(state->request, "null", 4);

	return FALSE;
}

#define BSON_OID_LENGTH 24

static bson_bool_t
_bson_as_json_visit_oid (const bson_iter_t *iter,
                         const char        *key,
                         const bson_oid_t  *oid,
                         void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;
	char str[BSON_OID_LENGTH + 1];

	if (oid != NULL) {
		bson_oid_to_string(oid, str);
		sepia_send_data(state->request, "{ \"$oid\" : \"", 13);
		sepia_send_data(state->request, str, BSON_OID_LENGTH);
		sepia_send_data(state->request, "\" }", 3);
	}

   return FALSE;
}

static bson_bool_t
_bson_as_json_visit_binary (const bson_iter_t  *iter,
                            const char         *key,
                            bson_subtype_t      v_subtype,
                            size_t              v_binary_len,
                            const bson_uint8_t *v_binary,
                            void               *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;
	size_t b64_len;
	char * b64;
	char str[3];

	b64_len = (v_binary_len / 3 + 1) * 4 + 1;
	b64 = bson_malloc0 (b64_len);
	// TODO stream b64 directly
	int b64_length = b64_ntop (v_binary, v_binary_len, b64, b64_len);

	sepia_send_data(state->request, "{ \"$type\" : \"", 14);
	int length = snprintf (str, sizeof str, "%02x", v_subtype);
	if (length >= 3) {
		length = 2;
	}
	sepia_send_data(state->request, str, length);
	sepia_send_data(state->request, "\", \"$binary\" : \"", 16);
	sepia_send_data(state->request, b64, b64_length);
	sepia_send_data(state->request, "\" }", 3);

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_bool (const bson_iter_t *iter,
                          const char        *key,
                          bson_bool_t        v_bool,
                          void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;

	if (v_bool) {
		sepia_send_data(state->request, "true", 4);
	} else {
		sepia_send_data(state->request, "false", 5);
	}

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_date_time (const bson_iter_t *iter,
                               const char        *key,
                               bson_int64_t       msec_since_epoch,
                               void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;
	char secstr[BUFFER_SIZE];

	int length = snprintf (secstr, sizeof secstr, "%" PRIi64, msec_since_epoch);
	if (length >= BUFFER_SIZE) {
		length = BUFFER_SIZE - 1;
	}
	sepia_send_data(state->request, "{ \"$date\" : ", 12);
	sepia_send_data(state->request, secstr, length);
	sepia_send_data(state->request, " }", 2);

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_regex (const bson_iter_t *iter,
                           const char        *key,
                           const char        *v_regex,
                           const char        *v_options,
                           void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;

	sepia_send_data(state->request, "{ \"$regex\" : \"", 14);
	sepia_send_data(state->request, v_regex, strlen(v_regex));
	sepia_send_data(state->request, "\", \"$options\" : \"", 17);
	sepia_send_data(state->request, v_options, strlen(v_options));
	sepia_send_data(state->request, "\" }", 3);

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_timestamp (const bson_iter_t *iter,
                               const char        *key,
                               bson_uint32_t      v_timestamp,
                               bson_uint32_t      v_increment,
                               void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;
	char str[BUFFER_SIZE];

	sepia_send_data(state->request, "{ \"$timestamp\" : { \"t\": ", 24);
	int length = snprintf (str, sizeof str, "%u", v_timestamp);
	if (length >= BUFFER_SIZE) {
		length = BUFFER_SIZE - 1;
	}
	sepia_send_data(state->request, str, length);

	sepia_send_data(state->request, ", \"i\": ", 7);
	length = snprintf (str, sizeof str, "%u", v_increment);
	if (length >= BUFFER_SIZE) {
		length = BUFFER_SIZE - 1;
	}
	sepia_send_data(state->request, str, length);
	sepia_send_data(state->request, " } }", 4);

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_dbpointer (const bson_iter_t *iter,
                               const char        *key,
                               size_t             v_collection_len,
                               const char        *v_collection,
                               const bson_oid_t  *v_oid,
                               void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;
	char str[BSON_OID_LENGTH + 1];

	sepia_send_data(state->request, "{ \"$ref\" : \"", 12);
	sepia_send_data(state->request, v_collection, v_collection_len);
	sepia_send_data(state->request, "\"", 1);

	if (v_oid != NULL) {
		bson_oid_to_string(v_oid, str);
		sepia_send_data(state->request, ", \"$id\" : \"", 12);
		sepia_send_data(state->request, str, BSON_OID_LENGTH);
		sepia_send_data(state->request, "\"", 1);
	}

	sepia_send_data(state->request, " }", 2);

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_minkey (const bson_iter_t *iter,
                            const char        *key,
                            void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;

	sepia_send_data(state->request, "{ \"$minKey\" : 1 }", 17);

	return FALSE;
}


static bson_bool_t
_bson_as_json_visit_maxkey (const bson_iter_t *iter,
                            const char        *key,
                            void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;

	sepia_send_data(state->request, "{ \"$maxKey\" : 1 }", 17);

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_before (const bson_iter_t *iter,
                            const char        *key,
                            void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;

	if (state->count) {
		sepia_send_data(state->request, ", ", 2);
	}

	if (state->keys) {
		sepia_send_data(state->request, "\"", 1);
		sepia_send_utf8(state->request, key, -1);
		sepia_send_data(state->request, "\" : ", 4);
	}

	state->count++;

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_code (const bson_iter_t *iter,
                          const char        *key,
                          size_t             v_code_len,
                          const char        *v_code,
                          void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;

	sepia_send_data(state->request, "\"", 1);
	sepia_send_data(state->request, v_code, v_code_len);
	sepia_send_data(state->request, "\"", 1);

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_symbol (const bson_iter_t *iter,
                            const char        *key,
                            size_t             v_symbol_len,
                            const char        *v_symbol,
                            void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;

	sepia_send_data(state->request, "\"", 1);
	sepia_send_data(state->request, v_symbol, v_symbol_len);
	sepia_send_data(state->request, "\"", 1);

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_codewscope (const bson_iter_t *iter,
                                const char        *key,
                                size_t             v_code_len,
                                const char        *v_code,
                                const bson_t      *v_scope,
                                void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;

	sepia_send_data(state->request, "\"", 1);
	sepia_send_data(state->request, v_code, v_code_len);
	sepia_send_data(state->request, "\"", 1);

	return FALSE;
}

static const bson_visitor_t bson_as_json_visitors = {
	.visit_before     = _bson_as_json_visit_before,
	.visit_double     = _bson_as_json_visit_double,
	.visit_utf8       = _bson_as_json_visit_utf8,
	.visit_document   = _bson_as_json_visit_document,
	.visit_array      = _bson_as_json_visit_array,
	.visit_binary     = _bson_as_json_visit_binary,
	.visit_undefined  = _bson_as_json_visit_undefined,
	.visit_oid        = _bson_as_json_visit_oid,
	.visit_bool       = _bson_as_json_visit_bool,
	.visit_date_time  = _bson_as_json_visit_date_time,
	.visit_null       = _bson_as_json_visit_null,
	.visit_regex      = _bson_as_json_visit_regex,
	.visit_dbpointer  = _bson_as_json_visit_dbpointer,
	.visit_code       = _bson_as_json_visit_code,
	.visit_symbol     = _bson_as_json_visit_symbol,
	.visit_codewscope = _bson_as_json_visit_codewscope,
	.visit_int32      = _bson_as_json_visit_int32,
	.visit_timestamp  = _bson_as_json_visit_timestamp,
	.visit_int64      = _bson_as_json_visit_int64,
	.visit_minkey     = _bson_as_json_visit_minkey,
	.visit_maxkey     = _bson_as_json_visit_maxkey,
};

#define BSON_MAX_RECURSION 1024

static bson_bool_t
_bson_as_json_visit_document (const bson_iter_t *iter,
                              const char        *key,
                              const bson_t      *v_document,
                              void              *data)
{
	struct bson_json_state * state = (struct bson_json_state *) data;
	struct bson_json_state child_state = { 0, TRUE };
	bson_iter_t child;

	if (state->depth >= BSON_MAX_RECURSION) {
		sepia_send_data(state->request, "{ ... }", 7);
		return FALSE;
	}

	if (bson_iter_init (&child, v_document)) {
		child_state.request = state->request;
		sepia_send_data(state->request, "{ ", 2);
		child_state.depth = state->depth + 1;
		bson_iter_visit_all (&child, &bson_as_json_visitors, &child_state);
		sepia_send_data(state->request, " }", 2);
	}

	return FALSE;
}

static bson_bool_t
_bson_as_json_visit_array (const bson_iter_t *iter,
                           const char        *key,
                           const bson_t      *v_array,
                           void              *data)
{
	struct bson_json_state * state = data;
	struct bson_json_state child_state = { 0, FALSE };
	bson_iter_t child;

	if (state->depth >= BSON_MAX_RECURSION) {
		sepia_send_data(state->request, "{ ... }", 7);
		return FALSE;
	}

	if (bson_iter_init (&child, v_array)) {
		child_state.request = state->request;
		sepia_send_data(state->request, "[ ", 2);
		child_state.depth = state->depth + 1;
		bson_iter_visit_all (&child, &bson_as_json_visitors, &child_state);
		sepia_send_data(state->request, " ]", 2);
	}

	return FALSE;
}

void sepia_send_json(struct sepia_request * request, bson_t * bson)
{
	if (sepia_request_status(request) != SEPIA_REQUEST_HEADERS_SEND) {
		sepia_send_header(request, &CONTENT_TYPE, &CONTENT_TYPE_APPLICATION_JSON);
	}

	if (bson == NULL) return;

	if (bson_empty(bson)) {
		sepia_send_string(request, &JSON_START);
		sepia_send_string(request, &JSON_END);
		return;
	}

	bson_iter_t iter;
	struct bson_json_state state;
	state.count = 0;
	state.depth = 0;
	state.keys = TRUE;
	state.request = request;

	if (!bson_iter_init (&iter, bson)) {
		return;
	}

	sepia_send_string(request, &JSON_START);
	bson_iter_visit_all (&iter, &bson_as_json_visitors, &state);

	if (iter.err_off) {
		// TODO log error message
		return;
	}

	sepia_send_string(request, &JSON_END);
}

