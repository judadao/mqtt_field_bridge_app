#ifndef PRODUCT_ETHERNET_H
#define PRODUCT_ETHERNET_H

#include <stddef.h>

#include "product_config.h"

int product_ethernet_start(const field_bridge_settings_t *settings,
                           char *ip_addr,
                           size_t ip_addr_cap);
int product_ethernet_link_ready(void);

#endif /* PRODUCT_ETHERNET_H */
