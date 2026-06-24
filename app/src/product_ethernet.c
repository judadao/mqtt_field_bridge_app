#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include "product_ethernet.h"

LOG_MODULE_REGISTER(product_ethernet, LOG_LEVEL_INF);

#ifndef __ZEPHYR__

int product_ethernet_start(const field_bridge_settings_t *settings,
                           char *ip_addr,
                           size_t ip_addr_cap)
{
    if (!settings || !ip_addr || ip_addr_cap == 0) {
        return -1;
    }
    snprintf(ip_addr, ip_addr_cap, "%s",
             settings->network.device_ip[0] ?
             settings->network.device_ip : "0.0.0.0");
    return 0;
}

#else

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/ethernet_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_l2.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dhcpv4.h>

#define ETHERNET_DHCP_TIMEOUT_S 3
#define ETHERNET_CARRIER_WAIT_S 5
#define ETHERNET_FALLBACK_IP    "192.168.127.4"
#define ETHERNET_FALLBACK_GW    "192.168.127.5"
#define ETHERNET_FALLBACK_MASK  "255.255.0.0"

static K_SEM_DEFINE(ethernet_ip_sem, 0, 1);

static struct net_mgmt_event_callback ethernet_ipv4_cb;
static struct net_mgmt_event_callback ethernet_l2_cb;
static struct net_if *ethernet_iface;
static int ethernet_callbacks_ready;

static void ethernet_ipv4_event_handler(struct net_mgmt_event_callback *cb,
                                        uint64_t event,
                                        struct net_if *iface)
{
    ARG_UNUSED(cb);

    if (event == NET_EVENT_IPV4_ADDR_ADD && iface == ethernet_iface) {
        LOG_INF("Ethernet IPv4 address obtained");
        k_sem_give(&ethernet_ip_sem);
    }
}

static void ethernet_l2_event_handler(struct net_mgmt_event_callback *cb,
                                      uint64_t event,
                                      struct net_if *iface)
{
    ARG_UNUSED(cb);

    if (iface != ethernet_iface) {
        return;
    }

    if (event == NET_EVENT_ETHERNET_CARRIER_ON) {
        LOG_INF("Ethernet carrier on");
    } else if (event == NET_EVENT_ETHERNET_CARRIER_OFF) {
        LOG_WRN("Ethernet carrier off");
    }
}

static void ensure_callbacks(void)
{
    if (ethernet_callbacks_ready) {
        return;
    }

    net_mgmt_init_event_callback(&ethernet_ipv4_cb,
                                 ethernet_ipv4_event_handler,
                                 NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&ethernet_ipv4_cb);
    net_mgmt_init_event_callback(&ethernet_l2_cb,
                                 ethernet_l2_event_handler,
                                 NET_EVENT_ETHERNET_CARRIER_ON |
                                 NET_EVENT_ETHERNET_CARRIER_OFF);
    net_mgmt_add_event_callback(&ethernet_l2_cb);
    ethernet_callbacks_ready = 1;
}

static void copy_ip(struct net_if *iface, char *out, size_t cap,
                    const char *fallback)
{
    struct net_in_addr *addr;

    if (!out || cap == 0) {
        return;
    }

    out[0] = '\0';
    addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_DHCP);
    if (!addr) {
        addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_MANUAL);
    }
    if (addr && net_addr_ntop(AF_INET, addr, out, cap)) {
        return;
    }
    snprintf(out, cap, "%s", fallback ? fallback : "0.0.0.0");
}

static void format_mac(struct net_if *iface, char *out, size_t cap)
{
    struct net_linkaddr *link_addr = net_if_get_link_addr(iface);

    if (!out || cap == 0) {
        return;
    }
    if (!link_addr || link_addr->len < 6) {
        snprintf(out, cap, "unknown");
        return;
    }
    snprintf(out, cap, "%02x:%02x:%02x:%02x:%02x:%02x",
             link_addr->addr[0], link_addr->addr[1], link_addr->addr[2],
             link_addr->addr[3], link_addr->addr[4], link_addr->addr[5]);
}

static void log_ethernet_state(const char *stage, struct net_if *iface)
{
    char mac[18];
    char ip[FIELD_BRIDGE_HOST_MAX];

    format_mac(iface, mac, sizeof(mac));
    copy_ip(iface, ip, sizeof(ip), "0.0.0.0");
    LOG_INF("Ethernet %s iface_up=%u carrier=%u mac=%s ip=%s",
            stage,
            net_if_is_up(iface) ? 1 : 0,
            net_if_is_carrier_ok(iface) ? 1 : 0,
            mac,
            ip);
}

static int wait_for_carrier(struct net_if *iface)
{
    for (int i = 0; i < ETHERNET_CARRIER_WAIT_S * 10; ++i) {
        if (net_if_is_carrier_ok(iface)) {
            log_ethernet_state("carrier-ready", iface);
            return 0;
        }
        k_msleep(100);
    }

    log_ethernet_state("carrier-timeout", iface);
    return -ETIMEDOUT;
}

static int configure_static_ipv4(struct net_if *iface,
                                 const field_bridge_settings_t *settings)
{
    struct net_in_addr addr;
    struct net_in_addr netmask;
    struct net_in_addr gateway;
    const char *ip = settings->network.device_ip[0] ?
        settings->network.device_ip : "192.168.1.50";
    const char *mask = settings->network.netmask[0] ?
        settings->network.netmask : "255.255.255.0";
    const char *gw = settings->network.gateway[0] ?
        settings->network.gateway : "192.168.1.1";

    if (net_addr_pton(AF_INET, ip, &addr) != 0 ||
        net_addr_pton(AF_INET, mask, &netmask) != 0 ||
        net_addr_pton(AF_INET, gw, &gateway) != 0) {
        LOG_ERR("invalid Ethernet static IPv4 ip=%s mask=%s gw=%s",
                ip, mask, gw);
        return -EINVAL;
    }

    if (!net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0)) {
        LOG_ERR("failed to assign Ethernet IPv4 %s", ip);
        return -EIO;
    }
    if (!net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask)) {
        LOG_ERR("failed to set Ethernet netmask %s", mask);
        return -EIO;
    }
    net_if_ipv4_set_gw(iface, &gateway);
    return 0;
}

static int configure_fallback_ipv4(struct net_if *iface)
{
    field_bridge_settings_t fallback;

    memset(&fallback, 0, sizeof(fallback));
    snprintf(fallback.network.device_ip, sizeof(fallback.network.device_ip),
             "%s", ETHERNET_FALLBACK_IP);
    snprintf(fallback.network.gateway, sizeof(fallback.network.gateway),
             "%s", ETHERNET_FALLBACK_GW);
    snprintf(fallback.network.netmask, sizeof(fallback.network.netmask),
             "%s", ETHERNET_FALLBACK_MASK);

    return configure_static_ipv4(iface, &fallback);
}

int product_ethernet_start(const field_bridge_settings_t *settings,
                           char *ip_addr,
                           size_t ip_addr_cap)
{
    int using_fallback = 0;

    if (!settings || !ip_addr || ip_addr_cap == 0) {
        return -EINVAL;
    }
    ip_addr[0] = '\0';

    ensure_callbacks();
    ethernet_iface = net_if_get_first_by_type(&NET_L2_GET_NAME(ETHERNET));
    if (!ethernet_iface) {
        LOG_ERR("no Ethernet interface");
        return -ENODEV;
    }

    net_if_set_default(ethernet_iface);
    net_if_up(ethernet_iface);
    log_ethernet_state("after-if-up", ethernet_iface);
    if (wait_for_carrier(ethernet_iface) != 0) {
        LOG_WRN("Ethernet carrier not detected; check W5500 RJ45 cable/link LEDs");
    }

    if (settings->network.dhcp_enabled) {
        k_sem_reset(&ethernet_ip_sem);
        LOG_INF("Starting Ethernet DHCP");
        net_dhcpv4_start(ethernet_iface);
        if (k_sem_take(&ethernet_ip_sem,
                       K_SECONDS(ETHERNET_DHCP_TIMEOUT_S)) != 0) {
            LOG_WRN("Ethernet DHCP timeout; using static fallback %s",
                    ETHERNET_FALLBACK_IP);
            net_dhcpv4_stop(ethernet_iface);
            if (configure_fallback_ipv4(ethernet_iface) != 0) {
                LOG_ERR("Ethernet static fallback failed");
                return -ETIMEDOUT;
            }
            using_fallback = 1;
        }
    } else {
        int rc = configure_static_ipv4(ethernet_iface, settings);
        if (rc != 0) {
            return rc;
        }
    }

    if (using_fallback) {
        snprintf(ip_addr, ip_addr_cap, "%s", ETHERNET_FALLBACK_IP);
    } else {
        copy_ip(ethernet_iface, ip_addr, ip_addr_cap,
                settings->network.device_ip[0] ?
                settings->network.device_ip : "0.0.0.0");
    }
    log_ethernet_state("ready", ethernet_iface);
    LOG_INF("Ethernet ready ip=%s", ip_addr);
    return 0;
}

#endif
