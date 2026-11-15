#pragma once
#include <stdbool.h>
#include <stddef.h>
/* Streaming JSON projection: retain traffic fields without buffering geometry
 * or impacted-object trees. Still validate syntax in discarded subtrees. */
typedef struct transport_json_filter transport_json_filter_t;
transport_json_filter_t *transport_json_filter_create(char *output,size_t capacity);
bool transport_json_filter_feed(transport_json_filter_t *filter,const char *data,size_t length);
bool transport_json_filter_finish(transport_json_filter_t *filter,size_t *length);
void transport_json_filter_free(transport_json_filter_t *filter);

transport_json_filter_t *transport_json_filter_create_network(char *output,size_t capacity);

transport_json_filter_t *transport_json_filter_create_siri(char *output,size_t capacity);
