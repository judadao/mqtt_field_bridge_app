#include <errno.h>
#include <stdio.h>
#include <string.h>

#if !defined(__ZEPHYR__) || defined(CONFIG_DEPHY_WIFI)
#include <dephy_wifi/wifi.h>
#define PRODUCT_HAS_DEPHY_WIFI 1
#else
#define PRODUCT_HAS_DEPHY_WIFI 0
#endif

#include "product_wifi.h"

#if defined(__ZEPHYR__) && defined(CONFIG_HWINFO)
#include <zephyr/drivers/hwinfo.h>
#endif

#if PRODUCT_HAS_DEPHY_WIFI
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

static int is_zero_ip(const char *ip)
{
    return !ip || !ip[0] || strcmp(ip, "0.0.0.0") == 0;
}

static uint8_t derived_host_octet(void)
{
    uint32_t hash = 2166136261u;

#if defined(__ZEPHYR__) && defined(CONFIG_HWINFO)
    uint8_t device_id[32];
    ssize_t len = hwinfo_get_device_id(device_id, sizeof(device_id));

    if (len > 0) {
        for (ssize_t i = 0; i < len; i++) {
            hash ^= device_id[i];
            hash *= 16777619u;
        }
    }
#endif

    return (uint8_t)(20u + (hash % 180u));
}

static void choose_sta_ip(const field_bridge_settings_t *settings,
                          char *out,
                          size_t out_cap)
{
    if (!settings->network.dhcp_enabled &&
        is_zero_ip(settings->network.device_ip)) {
        snprintf(out, out_cap, "%s.%u",
                 CONFIG_FIELD_BRIDGE_WIFI_STATIC_PREFIX,
                 derived_host_octet());
        return;
    }
    copy_field(out, out_cap, settings->network.device_ip);
}

static void build_wifi_settings(const field_bridge_settings_t *settings,
                                dephy_wifi_settings_t *wifi_settings)
{
    char sta_ip[DEPHY_WIFI_HOST_MAX];

    memset(wifi_settings, 0, sizeof(*wifi_settings));
    copy_field(wifi_settings->wifi_ssid, sizeof(wifi_settings->wifi_ssid),
               settings->network.wifi_ssid[0] ?
               settings->network.wifi_ssid :
               CONFIG_FIELD_BRIDGE_WIFI_STA_SSID);
    copy_field(wifi_settings->wifi_password, sizeof(wifi_settings->wifi_password),
               settings->network.wifi_password[0] ?
               settings->network.wifi_password :
               CONFIG_FIELD_BRIDGE_WIFI_STA_PASSWORD);
    copy_field(wifi_settings->ap_ssid, sizeof(wifi_settings->ap_ssid),
               CONFIG_FIELD_BRIDGE_WIFI_AP_SSID);
    copy_field(wifi_settings->ap_password, sizeof(wifi_settings->ap_password),
               CONFIG_FIELD_BRIDGE_WIFI_AP_PASSWORD);
    choose_sta_ip(settings, sta_ip, sizeof(sta_ip));
    copy_field(wifi_settings->device_ip, sizeof(wifi_settings->device_ip),
               sta_ip);
    copy_field(wifi_settings->gateway, sizeof(wifi_settings->gateway),
               is_zero_ip(settings->network.gateway) ?
               CONFIG_FIELD_BRIDGE_WIFI_STATIC_GATEWAY :
               settings->network.gateway);
    copy_field(wifi_settings->netmask, sizeof(wifi_settings->netmask),
               is_zero_ip(settings->network.netmask) ?
               CONFIG_FIELD_BRIDGE_WIFI_STATIC_NETMASK :
               settings->network.netmask);
    copy_field(wifi_settings->dns, sizeof(wifi_settings->dns),
               is_zero_ip(settings->network.dns) ?
               CONFIG_FIELD_BRIDGE_WIFI_STATIC_GATEWAY :
               settings->network.dns);
    wifi_settings->dhcp_enabled = settings->network.dhcp_enabled;
}
#endif

int product_wifi_start(const field_bridge_settings_t *settings,
                       char *ip_addr,
                       size_t ip_addr_cap)
{
#if PRODUCT_HAS_DEPHY_WIFI
    dephy_wifi_settings_t wifi_settings;

    if (!settings || !ip_addr || ip_addr_cap == 0) {
        return -EINVAL;
    }

    build_wifi_settings(settings, &wifi_settings);
    return dephy_wifi_start(&wifi_settings, ip_addr, ip_addr_cap);
#else
    (void)settings;
    (void)ip_addr;
    (void)ip_addr_cap;
    return -ENODEV;
#endif
}

int product_wifi_apply_settings(const field_bridge_settings_t *settings,
                                char *ip_addr,
                                size_t ip_addr_cap)
{
#if PRODUCT_HAS_DEPHY_WIFI
    dephy_wifi_settings_t wifi_settings;
    int rc;

    if (!settings || !ip_addr || ip_addr_cap == 0) {
        return -EINVAL;
    }

    build_wifi_settings(settings, &wifi_settings);
    rc = dephy_wifi_apply_settings(&wifi_settings);
    if (rc != 0) {
        return rc;
    }
    copy_field(ip_addr, ip_addr_cap, wifi_settings.device_ip);
    return 0;
#else
    (void)settings;
    (void)ip_addr;
    (void)ip_addr_cap;
    return -ENODEV;
#endif
}

int product_wifi_scan_json(char *buf, size_t buf_cap)
{
#if PRODUCT_HAS_DEPHY_WIFI
    return dephy_wifi_scan_json(buf, buf_cap);
#else
    if (!buf || buf_cap == 0) {
        return -EINVAL;
    }
    snprintf(buf, buf_cap, "[]");
    return -ENODEV;
#endif
}
