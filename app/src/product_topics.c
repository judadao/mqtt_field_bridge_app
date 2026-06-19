#include <stdio.h>
#include <string.h>

#include "product_topics.h"

int product_topic_build(const field_bridge_settings_t *settings,
                        const char *stream,
                        char *out,
                        int cap)
{
    if (!settings || !stream || !out || cap <= 0 ||
        settings->broker.topic_prefix[0] == '\0' || stream[0] == '\0' ||
        strchr(stream, '/') != NULL) {
        return -1;
    }

    int n = snprintf(out, (size_t)cap, "%s/%s",
                     settings->broker.topic_prefix, stream);
    if (n <= 0 || n >= cap) {
        return -1;
    }
    return 0;
}

int product_topic_status(const field_bridge_settings_t *settings, char *out, int cap)
{
    return product_topic_build(settings, FIELD_BRIDGE_STREAM_STATUS, out, cap);
}

int product_topic_io(const field_bridge_settings_t *settings, char *out, int cap)
{
    return product_topic_build(settings, FIELD_BRIDGE_STREAM_IO, out, cap);
}

int product_topic_event(const field_bridge_settings_t *settings, char *out, int cap)
{
    return product_topic_build(settings, FIELD_BRIDGE_STREAM_EVENT, out, cap);
}

int product_topic_test(const field_bridge_settings_t *settings, char *out, int cap)
{
    return product_topic_build(settings, FIELD_BRIDGE_STREAM_TEST, out, cap);
}

int product_topic_matches_prefix(const field_bridge_settings_t *settings,
                                 const char *topic)
{
    size_t prefix_len;

    if (!settings || !topic || settings->broker.topic_prefix[0] == '\0') {
        return 0;
    }
    prefix_len = strlen(settings->broker.topic_prefix);
    if (strncmp(topic, settings->broker.topic_prefix, prefix_len) != 0) {
        return 0;
    }
    return topic[prefix_len] == '/';
}
