#ifndef PRODUCT_WIFI_H
#define PRODUCT_WIFI_H

#include <stddef.h>

#include "product_config.h"

int product_wifi_start(const field_bridge_settings_t *settings,
                       char *ip_addr,
                       size_t ip_addr_cap);
int product_wifi_apply_settings(const field_bridge_settings_t *settings,
                                char *ip_addr,
                                size_t ip_addr_cap);
int product_wifi_scan_json(char *buf, size_t buf_cap);

#endif /* PRODUCT_WIFI_H */
