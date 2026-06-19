#ifndef PRODUCT_TOPICS_H
#define PRODUCT_TOPICS_H

#include "product_config.h"

#define FIELD_BRIDGE_STREAM_STATUS "status"
#define FIELD_BRIDGE_STREAM_IO     "io"
#define FIELD_BRIDGE_STREAM_EVENT  "event"
#define FIELD_BRIDGE_STREAM_TEST   "test"

int product_topic_build(const field_bridge_settings_t *settings,
                        const char *stream,
                        char *out,
                        int cap);
int product_topic_status(const field_bridge_settings_t *settings, char *out, int cap);
int product_topic_io(const field_bridge_settings_t *settings, char *out, int cap);
int product_topic_event(const field_bridge_settings_t *settings, char *out, int cap);
int product_topic_test(const field_bridge_settings_t *settings, char *out, int cap);
int product_topic_matches_prefix(const field_bridge_settings_t *settings,
                                 const char *topic);

#endif /* PRODUCT_TOPICS_H */
