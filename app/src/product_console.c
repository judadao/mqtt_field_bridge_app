#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dephy_cli/menu.h>

#include "product_config.h"
#include "product_console.h"
#include "product_runtime.h"
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
#include "product_wifi.h"
#endif

#ifdef __ZEPHYR__
#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(product_console, LOG_LEVEL_INF);
#endif

static void pc_write(product_console_write_fn write_fn, void *ctx,
                          const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    if (!write_fn) {
        return;
    }
    va_start(ap, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    write_fn(ctx, buf);
}

static void copy_arg(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) {
        return;
    }
    snprintf(dst, cap, "%s", src ? src : "");
}

static char *next_token(char **save)
{
    return strtok_r(NULL, " \t\r\n", save);
}

static int numeric_menu_command(const char *cmd)
{
    const char *p = cmd;
    int value = 0;

    if (!p || !*p) {
        return 0;
    }
    while (*p) {
        if (*p < '0' || *p > '9') {
            return 0;
        }
        value = (value * 10) + (*p - '0');
        p++;
    }
    return value;
}

static void print_help(product_console_write_fn write_fn, void *ctx)
{
    pc_write(write_fn, ctx,
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
                  "commands: help menu status info show wifi ip dhcp broker broker-state peer defaults reset reboot\n");
#else
                  "commands: help menu status info show ip dhcp broker broker-state peer defaults reset reboot\n");
#endif
    pc_write(write_fn, ctx,
                  "  info                     show runtime and saved network/broker config\n");
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
    pc_write(write_fn, ctx,
                  "  wifi <ssid> <pass|->     save WiFi AP, reboot to connect\n");
#endif
    pc_write(write_fn, ctx,
                  "  ip <addr> [gw] [mask]    save static network IP, reboot to apply\n");
    pc_write(write_fn, ctx,
                  "  dhcp                     enable DHCP, reboot to apply\n");
    pc_write(write_fn, ctx,
                  "  broker <mqtt> <p2p> [ip]  save local broker ports/ip\n");
    pc_write(write_fn, ctx,
                  "  broker-state <0|1>     disable/enable broker on next boot\n");
    pc_write(write_fn, ctx,
                  "  peer <i> <name> <host> [mqtt] [p2p] [0|1]\n");
    pc_write(write_fn, ctx,
                  "  defaults small|large      reset config profile\n");
}

static void print_menu(product_console_write_fn write_fn, void *ctx)
{
    static const dephy_cli_menu_item_t items[] = {
        { "status", "runtime network/broker state" },
        { "info", "runtime plus saved config" },
        { "show", "saved network/broker config" },
        { "ip <addr> [gw] [mask]", "set static IP" },
        { "dhcp", "enable DHCP" },
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
        { "wifi <ssid> <pass|->", "set WiFi AP" },
#else
        { "broker <mqtt> <p2p> [ip]", "set broker ports/IP" },
#endif
        { "broker-state <0|1>", "disable/enable broker" },
        { "peer <i> <name> <host> [mqtt] [p2p] [0|1]", "set bridge peer" },
        { "defaults small|large", "reset config profile" },
        { "reset", "reset all settings" },
        { "reboot", "restart device" },
    };

    (void)dephy_cli_render_menu("Field Bridge CLI menu",
                                items,
                                sizeof(items) / sizeof(items[0]),
                                "Type the command shown after the number, for example: status",
                                write_fn,
                                ctx);
}

static int cmd_status(product_console_write_fn write_fn, void *ctx)
{
    field_bridge_runtime_status_t status;

    if (product_runtime_get_status(&status) != 0) {
        pc_write(write_fn, ctx, "ERR status unavailable\n");
        return -1;
    }
    pc_write(write_fn, ctx, "Status\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Network       : %s\n", status.network_state);
    pc_write(write_fn, ctx, "IP            : %s\n", status.ip_addr);
    pc_write(write_fn, ctx, "Broker        : %s\n", status.broker_state);
    pc_write(write_fn, ctx, "P2P           : %s\n", status.p2p_role);
    pc_write(write_fn, ctx, "Peers         : %u\n", status.connected_peers);
    pc_write(write_fn, ctx, "Remote subs   : %u\n", status.remote_subscriptions);
    pc_write(write_fn, ctx, "Error         : %s\n",
             status.last_error[0] ? status.last_error : "-");
    return 0;
}

static int cmd_show(product_console_write_fn write_fn, void *ctx)
{
    field_bridge_settings_t settings;

    if (product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR config unavailable\n");
        return -1;
    }
    pc_write(write_fn, ctx, "Saved config\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Device        : %s\n", settings.system.device_name);
    pc_write(write_fn, ctx, "IP            : %s\n", settings.network.device_ip);
    pc_write(write_fn, ctx, "Gateway       : %s\n", settings.network.gateway);
    pc_write(write_fn, ctx, "Netmask       : %s\n", settings.network.netmask);
    pc_write(write_fn, ctx, "DNS           : %s\n", settings.network.dns);
    pc_write(write_fn, ctx, "DHCP          : %u\n", settings.network.dhcp_enabled);
    pc_write(write_fn, ctx, "Broker IP     : %s\n", settings.broker.broker_ip);
    pc_write(write_fn, ctx, "MQTT port     : %u\n", settings.broker.mqtt_port);
    pc_write(write_fn, ctx, "P2P port      : %u\n", settings.broker.p2p_port);
    pc_write(write_fn, ctx, "Bridge        : %u\n", settings.broker.bridge_enabled);
    pc_write(write_fn, ctx, "Mesh          : %u\n", settings.broker.mesh_enabled);
    return 0;
}

static int cmd_info(product_console_write_fn write_fn, void *ctx)
{
    field_bridge_runtime_status_t status;
    field_bridge_settings_t settings;

    if (product_runtime_get_status(&status) != 0 ||
        product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR info unavailable\n");
        return -1;
    }

    pc_write(write_fn, ctx, "Runtime\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Network       : %s\n", status.network_state);
    pc_write(write_fn, ctx, "IP            : %s\n", status.ip_addr);
    pc_write(write_fn, ctx, "Broker        : %s\n", status.broker_state);
    pc_write(write_fn, ctx, "P2P           : %s\n", status.p2p_role);
    pc_write(write_fn, ctx, "Peers         : %u\n", status.connected_peers);
    pc_write(write_fn, ctx, "Remote subs   : %u\n", status.remote_subscriptions);
    pc_write(write_fn, ctx, "Error         : %s\n",
             status.last_error[0] ? status.last_error : "-");
    pc_write(write_fn, ctx, "\nConfig\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Device        : %s\n", settings.system.device_name);
    pc_write(write_fn, ctx, "DHCP          : %u\n", settings.network.dhcp_enabled);
    pc_write(write_fn, ctx, "IP            : %s\n", settings.network.device_ip);
    pc_write(write_fn, ctx, "Gateway       : %s\n", settings.network.gateway);
    pc_write(write_fn, ctx, "Netmask       : %s\n", settings.network.netmask);
    pc_write(write_fn, ctx, "DNS           : %s\n", settings.network.dns);
    pc_write(write_fn, ctx, "Broker IP     : %s\n", settings.broker.broker_ip);
    pc_write(write_fn, ctx, "MQTT port     : %u\n", settings.broker.mqtt_port);
    pc_write(write_fn, ctx, "P2P port      : %u\n", settings.broker.p2p_port);
    pc_write(write_fn, ctx, "Broker        : %u\n", settings.broker.broker_enabled);
    pc_write(write_fn, ctx, "Bridge        : %u\n", settings.broker.bridge_enabled);
    pc_write(write_fn, ctx, "Mesh          : %u\n", settings.broker.mesh_enabled);
    return 0;
}

static int cmd_ip(char **save, product_console_write_fn write_fn, void *ctx)
{
    char *ip = next_token(save);
    char *gw = next_token(save);
    char *mask = next_token(save);
    field_bridge_settings_t settings;

    if (!ip || product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR usage: ip <addr> [gw] [mask]\n");
        return -1;
    }

    copy_arg(settings.network.device_ip, sizeof(settings.network.device_ip), ip);
    if (gw) {
        copy_arg(settings.network.gateway, sizeof(settings.network.gateway), gw);
        copy_arg(settings.network.dns, sizeof(settings.network.dns), gw);
    } else if (!settings.network.gateway[0]) {
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
        copy_arg(settings.network.gateway, sizeof(settings.network.gateway),
                 CONFIG_FIELD_BRIDGE_WIFI_STATIC_GATEWAY);
        copy_arg(settings.network.dns, sizeof(settings.network.dns),
                 CONFIG_FIELD_BRIDGE_WIFI_STATIC_GATEWAY);
#else
        copy_arg(settings.network.gateway, sizeof(settings.network.gateway), "192.168.127.5");
        copy_arg(settings.network.dns, sizeof(settings.network.dns), "192.168.127.5");
#endif
    }
    if (mask) {
        copy_arg(settings.network.netmask, sizeof(settings.network.netmask), mask);
    } else if (!settings.network.netmask[0]) {
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
        copy_arg(settings.network.netmask, sizeof(settings.network.netmask),
                 CONFIG_FIELD_BRIDGE_WIFI_STATIC_NETMASK);
#else
        copy_arg(settings.network.netmask, sizeof(settings.network.netmask), "255.255.0.0");
#endif
    }
    if (!settings.network.dns[0]) {
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
        copy_arg(settings.network.dns, sizeof(settings.network.dns),
                 settings.network.gateway[0] ?
                 settings.network.gateway : CONFIG_FIELD_BRIDGE_WIFI_STATIC_GATEWAY);
#else
        copy_arg(settings.network.dns, sizeof(settings.network.dns),
                 settings.network.gateway[0] ? settings.network.gateway : "192.168.127.5");
#endif
    }
    settings.network.dhcp_enabled = 0;

    if (product_config_set_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR save failed\n");
        return -1;
    }
    pc_write(write_fn, ctx,
                  "OK saved static ip=%s gw=%s mask=%s dns=%s; reboot to apply\n",
                  settings.network.device_ip,
                  settings.network.gateway,
                  settings.network.netmask,
                  settings.network.dns);
    return 0;
}

static int cmd_dhcp(product_console_write_fn write_fn, void *ctx)
{
    field_bridge_settings_t settings;

    if (product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR config unavailable\n");
        return -1;
    }
    settings.network.dhcp_enabled = 1;
    if (product_config_set_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR save failed\n");
        return -1;
    }
    pc_write(write_fn, ctx, "OK saved DHCP enabled; reboot to apply\n");
    return 0;
}

#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
static int cmd_wifi(char **save, product_console_write_fn write_fn, void *ctx)
{
    char *ssid = next_token(save);
    char *pass = next_token(save);
    field_bridge_settings_t settings;

    if (!ssid || !pass || product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx,
                 "ERR usage: wifi <ssid> <pass|->\n");
        return -1;
    }
    if (strcmp(settings.network.device_ip, "0.0.0.0") == 0 ||
        settings.network.device_ip[0] == '\0') {
        pc_write(write_fn, ctx, "ERR set static IP first: ip <addr> [gw] [mask]\n");
        return -1;
    }

    copy_arg(settings.network.wifi_ssid, sizeof(settings.network.wifi_ssid),
             ssid);
    copy_arg(settings.network.wifi_password, sizeof(settings.network.wifi_password),
             strcmp(pass, "-") == 0 ? "" : pass);
    copy_arg(settings.broker.broker_ip, sizeof(settings.broker.broker_ip),
             settings.network.device_ip);
    settings.network.dhcp_enabled = 0;

    if (product_config_set_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR save wifi failed\n");
        return -1;
    }
    pc_write(write_fn, ctx,
             "OK saved wifi ssid=%s ip=%s gw=%s mask=%s; reboot to apply\n",
             settings.network.wifi_ssid,
             settings.network.device_ip,
             settings.network.gateway,
             settings.network.netmask);
    return 0;
}
#endif

static int cmd_broker(char **save, product_console_write_fn write_fn, void *ctx)
{
    char *mqtt = next_token(save);
    char *p2p = next_token(save);
    char *ip = next_token(save);
    field_bridge_settings_t settings;

    if (!mqtt || !p2p || product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR usage: broker <mqtt> <p2p> [ip]\n");
        return -1;
    }
    settings.broker.mqtt_port = (uint16_t)atoi(mqtt);
    settings.broker.p2p_port = (uint16_t)atoi(p2p);
    if (ip) {
        copy_arg(settings.broker.broker_ip, sizeof(settings.broker.broker_ip), ip);
    }
    if (product_config_set_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR save failed\n");
        return -1;
    }
    pc_write(write_fn, ctx, "OK saved broker config; reboot to apply\n");
    return 0;
}

static int cmd_broker_state(char **save, product_console_write_fn write_fn,
                            void *ctx)
{
    char *enabled = next_token(save);
    field_bridge_settings_t settings;
    int value;

    if (!enabled || product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR usage: broker-state <0|1>\n");
        return -1;
    }

    value = atoi(enabled);
    if (value != 0 && value != 1) {
        pc_write(write_fn, ctx, "ERR broker-state must be 0 or 1\n");
        return -1;
    }

    settings.broker.broker_enabled = (uint8_t)value;
    if (product_config_set_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR save failed\n");
        return -1;
    }
    pc_write(write_fn, ctx, "OK saved broker-state=%d; reboot to apply\n",
             value);
    return 0;
}

static int cmd_peer(char **save, product_console_write_fn write_fn, void *ctx)
{
    char *index_s = next_token(save);
    char *name = next_token(save);
    char *host = next_token(save);
    char *mqtt = next_token(save);
    char *p2p = next_token(save);
    char *enabled = next_token(save);
    field_bridge_peer_t peer;
    int index;

    if (!index_s || !name || !host) {
        pc_write(write_fn, ctx,
                      "ERR usage: peer <i> <name> <host> [mqtt] [p2p] [0|1]\n");
        return -1;
    }
    index = atoi(index_s);
    if (product_config_get_peer(index, &peer) != 0) {
        pc_write(write_fn, ctx, "ERR invalid peer index\n");
        return -1;
    }
    memset(&peer, 0, sizeof(peer));
    copy_arg(peer.name, sizeof(peer.name), name);
    copy_arg(peer.host, sizeof(peer.host), host);
    peer.mqtt_port = mqtt ? (uint16_t)atoi(mqtt) : 1883;
    peer.p2p_port = p2p ? (uint16_t)atoi(p2p) : 4884;
    peer.enabled = enabled ? (uint8_t)atoi(enabled) : 1;
    if (product_config_set_peer(index, &peer) != 0) {
        pc_write(write_fn, ctx, "ERR save peer failed\n");
        return -1;
    }
    pc_write(write_fn, ctx, "OK saved peer %d\n", index);
    return 0;
}

static int cmd_defaults(char **save, product_console_write_fn write_fn, void *ctx)
{
    char *profile = next_token(save);
    field_bridge_defaults_profile_t selected;

    if (!profile) {
        pc_write(write_fn, ctx, "ERR usage: defaults small|large\n");
        return -1;
    }
    if (strcmp(profile, "small") == 0) {
        selected = FIELD_BRIDGE_PROFILE_SMALL;
    } else if (strcmp(profile, "large") == 0) {
        selected = FIELD_BRIDGE_PROFILE_LARGE;
    } else {
        pc_write(write_fn, ctx, "ERR unknown defaults profile\n");
        return -1;
    }
    if (product_config_apply_defaults(selected) != 0) {
        pc_write(write_fn, ctx, "ERR defaults failed\n");
        return -1;
    }
    pc_write(write_fn, ctx, "OK defaults applied; reboot to apply network/broker\n");
    return 0;
}

static int cmd_menu_index(int index,
                          product_console_write_fn write_fn,
                          void *write_ctx,
                          product_console_reboot_fn reboot_fn,
                          void *reboot_ctx)
{
    switch (index) {
    case 1:
        return cmd_status(write_fn, write_ctx);
    case 2:
        return cmd_info(write_fn, write_ctx);
    case 3:
        return cmd_show(write_fn, write_ctx);
    case 4:
        pc_write(write_fn, write_ctx, "usage: ip <addr> [gw] [mask]\n");
        return 0;
    case 5:
        return cmd_dhcp(write_fn, write_ctx);
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
    case 6:
        pc_write(write_fn, write_ctx, "usage: wifi <ssid> <pass|->\n");
        return 0;
    case 7:
#else
    case 6:
#endif
        pc_write(write_fn, write_ctx, "usage: broker-state <0|1>\n");
        return 0;
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
    case 8:
#else
    case 7:
#endif
        pc_write(write_fn, write_ctx,
                 "usage: peer <i> <name> <host> [mqtt] [p2p] [0|1]\n");
        return 0;
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
    case 9:
#else
    case 8:
#endif
        pc_write(write_fn, write_ctx, "usage: defaults small|large\n");
        return 0;
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
    case 10:
#else
    case 9:
#endif
        if (product_config_reset_all() != 0) {
            pc_write(write_fn, write_ctx, "ERR reset failed\n");
            return -1;
        }
        pc_write(write_fn, write_ctx, "OK reset defaults; reboot to apply\n");
        return 0;
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
    case 11:
#else
    case 10:
#endif
        pc_write(write_fn, write_ctx, "OK rebooting\n");
        if (reboot_fn) {
            reboot_fn(reboot_ctx);
        }
        return 0;
    default:
        pc_write(write_fn, write_ctx, "ERR unknown menu index; try menu\n");
        return -1;
    }
}

int product_console_handle_line(char *line,
                                product_console_write_fn write_fn,
                                void *write_ctx,
                                product_console_reboot_fn reboot_fn,
                                void *reboot_ctx)
{
    char *save = NULL;
    char *cmd;

    if (!line) {
        return -1;
    }
    cmd = strtok_r(line, " \t\r\n", &save);
    if (!cmd) {
        return 0;
    }
    {
        int index = numeric_menu_command(cmd);

        if (index > 0) {
            return cmd_menu_index(index, write_fn, write_ctx, reboot_fn, reboot_ctx);
        }
    }
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        print_help(write_fn, write_ctx);
        return 0;
    }
    if (strcmp(cmd, "menu") == 0) {
        print_menu(write_fn, write_ctx);
        return 0;
    }
    if (strcmp(cmd, "status") == 0) {
        return cmd_status(write_fn, write_ctx);
    }
    if (strcmp(cmd, "show") == 0) {
        return cmd_show(write_fn, write_ctx);
    }
    if (strcmp(cmd, "info") == 0) {
        return cmd_info(write_fn, write_ctx);
    }
    if (strcmp(cmd, "ip") == 0) {
        return cmd_ip(&save, write_fn, write_ctx);
    }
    if (strcmp(cmd, "dhcp") == 0) {
        return cmd_dhcp(write_fn, write_ctx);
    }
#if defined(CONFIG_FIELD_BRIDGE_WIFI_TEST_PROFILE)
    if (strcmp(cmd, "wifi") == 0) {
        return cmd_wifi(&save, write_fn, write_ctx);
    }
#endif
    if (strcmp(cmd, "broker") == 0) {
        return cmd_broker(&save, write_fn, write_ctx);
    }
    if (strcmp(cmd, "broker-state") == 0) {
        return cmd_broker_state(&save, write_fn, write_ctx);
    }
    if (strcmp(cmd, "peer") == 0) {
        return cmd_peer(&save, write_fn, write_ctx);
    }
    if (strcmp(cmd, "defaults") == 0) {
        return cmd_defaults(&save, write_fn, write_ctx);
    }
    if (strcmp(cmd, "reset") == 0) {
        if (product_config_reset_all() != 0) {
            pc_write(write_fn, write_ctx, "ERR reset failed\n");
            return -1;
        }
        pc_write(write_fn, write_ctx, "OK reset defaults; reboot to apply\n");
        return 0;
    }
    if (strcmp(cmd, "reboot") == 0) {
        pc_write(write_fn, write_ctx, "OK rebooting\n");
        if (reboot_fn) {
            reboot_fn(reboot_ctx);
        }
        return 0;
    }
    pc_write(write_fn, write_ctx, "ERR unknown command; try help\n");
    return -1;
}

#ifdef __ZEPHYR__
static void zephyr_console_write(void *ctx, const char *text)
{
    ARG_UNUSED(ctx);
    k_sleep(K_MSEC(5));
    printk("%s", text);
    k_sleep(K_MSEC(5));
}

static void zephyr_console_reboot(void *ctx)
{
    ARG_UNUSED(ctx);
    sys_reboot(SYS_REBOOT_COLD);
}

static void console_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    console_getline_init();
    printk("\nfield-bridge console ready. type help\n");
    while (1) {
        char *line = console_getline();

        if (!line) {
            k_sleep(K_MSEC(50));
            continue;
        }
        (void)product_console_handle_line(line,
                                          zephyr_console_write,
                                          NULL,
                                          zephyr_console_reboot,
                                          NULL);
    }
}

K_THREAD_STACK_DEFINE(console_stack, 3072);
static struct k_thread console_thread_data;
static int console_started;

void product_console_start(void)
{
    if (console_started) {
        return;
    }
    k_thread_create(&console_thread_data,
                    console_stack,
                    K_THREAD_STACK_SIZEOF(console_stack),
                    console_thread,
                    NULL, NULL, NULL,
                    9, 0, K_NO_WAIT);
    k_thread_name_set(&console_thread_data, "product_console");
    console_started = 1;
    LOG_INF("UART console control started");
}
#else
void product_console_start(void)
{
}
#endif
