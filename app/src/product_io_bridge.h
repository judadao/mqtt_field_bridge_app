#ifndef PRODUCT_IO_BRIDGE_H
#define PRODUCT_IO_BRIDGE_H

#include <stddef.h>

#include <dephy_industrial_io/industrial_io.h>

int product_io_bridge_format_state(char *topic,
                                   size_t topic_size,
                                   char *payload,
                                   size_t payload_size,
                                   const char *site,
                                   const char *node,
                                   const dephy_io_sample_t *sample);

#endif
