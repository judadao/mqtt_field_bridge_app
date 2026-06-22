#include "product_io_bridge.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    char topic[128];
    char payload[128];
    dephy_io_sample_t sample = {
        .name = "door",
        .type = DEPHY_IO_DI,
        .value = 1,
        .fault = 0,
        .changed_at_ms = 42,
    };

    assert(product_io_bridge_format_state(topic,
                                          sizeof(topic),
                                          payload,
                                          sizeof(payload),
                                          "factory-a",
                                          "node-1",
                                          &sample) == 0);
    assert(strcmp(topic, "site/factory-a/node/node-1/io/door/state") == 0);
    assert(strcmp(payload, "{\"type\":\"di\",\"value\":1,\"fault\":0,\"t_ms\":42}") == 0);
    assert(product_io_bridge_format_state(topic, 8, payload, sizeof(payload),
                                          "factory-a", "node-1", &sample) != 0);
    return 0;
}
