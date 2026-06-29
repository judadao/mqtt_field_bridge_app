#include <errno.h>
#include <string.h>

#if !defined(__ZEPHYR__) || defined(CONFIG_DEPHY_ETH)
#include <dephy_eth/eth.h>
#define PRODUCT_HAS_DEPHY_ETH 1
#else
#define PRODUCT_HAS_DEPHY_ETH 0
#endif

#include "product_ethernet.h"

#if defined(__ZEPHYR__) && PRODUCT_HAS_DEPHY_ETH
#include <zephyr/net/ethernet.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_l2.h>
#endif

#if PRODUCT_HAS_DEPHY_ETH
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
#endif

int product_ethernet_start(const field_bridge_settings_t *settings,
                           char *ip_addr,
                           size_t ip_addr_cap)
{
#if PRODUCT_HAS_DEPHY_ETH
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
#else
    (void)settings;
    (void)ip_addr;
    (void)ip_addr_cap;
    return -ENODEV;
#endif
}

int product_ethernet_link_ready(void)
{
#if defined(__ZEPHYR__) && PRODUCT_HAS_DEPHY_ETH
    struct net_if *iface = net_if_get_first_by_type(&NET_L2_GET_NAME(ETHERNET));

    return iface && net_if_is_up(iface) && net_if_is_carrier_ok(iface);
#elif !defined(__ZEPHYR__)
    return 1;
#else
    return 0;
#endif
}
