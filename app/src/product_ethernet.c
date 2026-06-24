#include <errno.h>
#include <string.h>

#include <dephy_eth/eth.h>

#include "product_ethernet.h"

static void copy_field(char *dst, size_t dst_cap, const char *src)
{
    if (!dst || dst_cap == 0) {
        return;
    }
    dst[0] = '\0';
    if (src) {
        strncpy(dst, src, dst_cap - 1);
        dst[dst_cap - 1] = '\0';
    }
}

int product_ethernet_start(const field_bridge_settings_t *settings,
                           char *ip_addr,
                           size_t ip_addr_cap)
{
    dephy_eth_settings_t eth_settings;

    if (!settings || !ip_addr || ip_addr_cap == 0) {
        return -EINVAL;
    }

    memset(&eth_settings, 0, sizeof(eth_settings));
    copy_field(eth_settings.device_ip, sizeof(eth_settings.device_ip),
               settings->network.device_ip);
    copy_field(eth_settings.service_ip, sizeof(eth_settings.service_ip),
               settings->broker.broker_ip);
    copy_field(eth_settings.gateway, sizeof(eth_settings.gateway),
               settings->network.gateway);
    copy_field(eth_settings.netmask, sizeof(eth_settings.netmask),
               settings->network.netmask);
    copy_field(eth_settings.dns, sizeof(eth_settings.dns),
               settings->network.dns);
    eth_settings.dhcp_enabled = settings->network.dhcp_enabled;

    return dephy_eth_start(&eth_settings, ip_addr, ip_addr_cap);
}
