#include "product_io_bridge.h"

#include <dephy_industrial_io/payload.h>

int product_io_bridge_format_state(char *topic,
                                   size_t topic_size,
                                   char *payload,
                                   size_t payload_size,
                                   const char *site,
                                   const char *node,
                                   const dephy_io_sample_t *sample)
{
    if (!topic || !payload || !sample || !sample->name) {
        return -1;
    }
    if (dephy_io_format_topic(topic, topic_size, site, node, sample->name, "state") <= 0) {
        return -1;
    }
    if (dephy_io_format_sample_json(payload, payload_size, sample) <= 0) {
        return -1;
    }
    return 0;
}
