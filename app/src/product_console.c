#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dephy_cli/menu.h>

#include "product_config.h"
#include "product_console.h"
#include "product_runtime.h"

#ifdef __ZEPHYR__
#include <zephyr/device.h>
#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(product_console, LOG_LEVEL_INF);
#endif

typedef enum {
    PRODUCT_CONSOLE_MENU_MAIN = 0,
    PRODUCT_CONSOLE_MENU_INFO,
    PRODUCT_CONSOLE_MENU_SETTINGS,
    PRODUCT_CONSOLE_MENU_DEVICE_NETWORK,
    PRODUCT_CONSOLE_MENU_BROKER,
    PRODUCT_CONSOLE_MENU_PEER_BRIDGE,
    PRODUCT_CONSOLE_MENU_SYSTEM,
} product_console_menu_t;

static product_console_menu_t current_menu = PRODUCT_CONSOLE_MENU_MAIN;

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

static void print_back_to_menu(product_console_write_fn write_fn, void *ctx)
{
    pc_write(write_fn, ctx, "\n0. return to menu\n");
}

static void clear_screen(product_console_write_fn write_fn, void *ctx)
{
    pc_write(write_fn, ctx, "\033[2J\033[H");
}

static void print_help(product_console_write_fn write_fn, void *ctx)
{
    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx,
                  "commands: help menu back info settings system status summary show device-network local-broker peer-bridge mode ip dhcp broker-port broker-fallback-port broker-ip broker-state bridge-port peer defaults reset reboot\n");
    pc_write(write_fn, ctx,
                  "  info                     show info menu\n");
    pc_write(write_fn, ctx,
                  "  settings                 show settings menu\n");
    pc_write(write_fn, ctx,
                  "  system                   show system menu\n");
    pc_write(write_fn, ctx,
                  "  summary                  show runtime and saved network/broker config\n");
    pc_write(write_fn, ctx,
                  "  mode auto|eth            save Ethernet network mode\n");
    pc_write(write_fn, ctx,
                  "  ip <addr> [gw] [mask]    save static network IP, reboot to apply\n");
    pc_write(write_fn, ctx,
                  "  dhcp                     enable DHCP, reboot to apply\n");
    pc_write(write_fn, ctx,
                  "  broker-port <port>        save local broker client port\n");
    pc_write(write_fn, ctx,
                  "  broker-fallback-port <port> save local fallback MQTT port\n");
    pc_write(write_fn, ctx,
                  "  broker-ip <ip>            save local broker IP\n");
    pc_write(write_fn, ctx,
                  "  broker-state <0|1>     disable/enable broker on next boot\n");
    pc_write(write_fn, ctx,
                  "  bridge-port <port>        save local peer bridge port\n");
    pc_write(write_fn, ctx,
                  "  peer <i> <name> <host> [broker-port] [bridge-port] [0|1]\n");
    pc_write(write_fn, ctx,
                  "  defaults small|large      reset config profile\n");
}

static void print_menu(product_console_write_fn write_fn, void *ctx)
{
    static const dephy_cli_menu_item_t items[] = {
        { "info", "runtime and saved values" },
        { "settings", "network, broker, bridge" },
        { "system", "reset and reboot" },
    };

    current_menu = PRODUCT_CONSOLE_MENU_MAIN;
    clear_screen(write_fn, ctx);
    (void)dephy_cli_render_menu("Field Bridge CLI menu",
                                items,
                                sizeof(items) / sizeof(items[0]),
                                "Type a number to enter; 0 returns here",
                                write_fn,
                                ctx);
}

static void print_info_menu(product_console_write_fn write_fn, void *ctx)
{
    static const dephy_cli_menu_item_t items[] = {
        { "status", "runtime network/broker state" },
        { "summary", "runtime plus saved config" },
        { "show", "saved network/broker config" },
    };

    current_menu = PRODUCT_CONSOLE_MENU_INFO;
    clear_screen(write_fn, ctx);
    (void)dephy_cli_render_menu("Info",
                                items,
                                sizeof(items) / sizeof(items[0]),
                                "Type a number; 0 returns to main menu",
                                write_fn,
                                ctx);
}

static void print_settings_menu(product_console_write_fn write_fn, void *ctx)
{
    static const dephy_cli_menu_item_t items[] = {
        { "device-network", "device network setting" },
        { "local-broker", "local broker setting" },
        { "peer-bridge", "peer bridge setting" },
    };

    current_menu = PRODUCT_CONSOLE_MENU_SETTINGS;
    clear_screen(write_fn, ctx);
    (void)dephy_cli_render_menu("Settings",
                                items,
                                sizeof(items) / sizeof(items[0]),
                                "Type a number to enter; 0 returns to main menu",
                                write_fn,
                                ctx);
}

static void print_network_menu(product_console_write_fn write_fn, void *ctx)
{
    static const dephy_cli_menu_item_t items[] = {
        { "mode auto|eth", "set network mode" },
        { "ip <addr> [gw] [mask]", "set static IP" },
        { "dhcp", "enable DHCP" },
    };

    current_menu = PRODUCT_CONSOLE_MENU_DEVICE_NETWORK;
    clear_screen(write_fn, ctx);
    (void)dephy_cli_render_menu("Device Network Setting",
                                items,
                                sizeof(items) / sizeof(items[0]),
                                "Type the command with values; 0 returns to main menu",
                                write_fn,
                                ctx);
}

static void print_broker_menu(product_console_write_fn write_fn, void *ctx)
{
    static const dephy_cli_menu_item_t items[] = {
        { "broker-port <port>", "set local broker port" },
        { "broker-fallback-port <port>", "set fallback MQTT port" },
        { "broker-ip <ip>", "set local broker IP" },
        { "broker-state <0|1>", "disable/enable broker" },
    };

    current_menu = PRODUCT_CONSOLE_MENU_BROKER;
    clear_screen(write_fn, ctx);
    (void)dephy_cli_render_menu("Local Broker Setting",
                                items,
                                sizeof(items) / sizeof(items[0]),
                                "Type the command with values; 0 returns to main menu",
                                write_fn,
                                ctx);
}

static void print_peer_bridge_menu(product_console_write_fn write_fn, void *ctx)
{
    static const dephy_cli_menu_item_t items[] = {
        { "bridge-port <port>", "set local bridge port" },
        { "peer <i> <name> <host> [broker-port] [bridge-port] [0|1]", "set peer" },
        { "defaults small|large", "reset config profile" },
    };

    current_menu = PRODUCT_CONSOLE_MENU_PEER_BRIDGE;
    clear_screen(write_fn, ctx);
    (void)dephy_cli_render_menu("Peer Bridge Setting",
                                items,
                                sizeof(items) / sizeof(items[0]),
                                "Type the command with values; 0 returns to main menu",
                                write_fn,
                                ctx);
}

static void print_system_menu(product_console_write_fn write_fn, void *ctx)
{
    static const dephy_cli_menu_item_t items[] = {
        { "reset", "reset all settings" },
        { "reboot", "restart device" },
    };

    current_menu = PRODUCT_CONSOLE_MENU_SYSTEM;
    clear_screen(write_fn, ctx);
    (void)dephy_cli_render_menu("System",
                                items,
                                sizeof(items) / sizeof(items[0]),
                                "Type a number; 0 returns to main menu",
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
    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx, "Status\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Network       : %s\n", status.network_state);
    pc_write(write_fn, ctx, "IP            : %s\n", status.ip_addr);
    pc_write(write_fn, ctx, "Broker        : %s\n", status.broker_state);
    pc_write(write_fn, ctx, "Bridge        : %s\n", status.p2p_role);
    pc_write(write_fn, ctx, "Peers         : %u\n", status.connected_peers);
    pc_write(write_fn, ctx, "Remote subs   : %u\n", status.remote_subscriptions);
    pc_write(write_fn, ctx, "Error         : %s\n",
             status.last_error[0] ? status.last_error : "-");
    print_back_to_menu(write_fn, ctx);
    return 0;
}

static int cmd_show(product_console_write_fn write_fn, void *ctx)
{
    field_bridge_settings_t settings;

    if (product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR config unavailable\n");
        return -1;
    }
    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx, "Saved config\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Device        : %s\n", settings.system.device_name);
    pc_write(write_fn, ctx, "Mode          : %s\n",
             product_config_network_mode_name(settings.network.mode));
    pc_write(write_fn, ctx, "ETH IP        : %s\n", settings.network.device_ip);
    pc_write(write_fn, ctx, "ETH Gateway   : %s\n", settings.network.gateway);
    pc_write(write_fn, ctx, "ETH Netmask   : %s\n", settings.network.netmask);
    pc_write(write_fn, ctx, "ETH DNS       : %s\n", settings.network.dns);
    pc_write(write_fn, ctx, "ETH DHCP      : %u\n", settings.network.dhcp_enabled);
    pc_write(write_fn, ctx, "Broker IP     : %s\n", settings.broker.broker_ip);
    pc_write(write_fn, ctx, "Broker port   : %u\n", settings.broker.mqtt_port);
    pc_write(write_fn, ctx, "Fallback port : %u\n", settings.broker.fallback_port);
    pc_write(write_fn, ctx, "Bridge port   : %u\n", settings.broker.p2p_port);
    pc_write(write_fn, ctx, "Bridge        : %u\n", settings.broker.bridge_enabled);
    pc_write(write_fn, ctx, "Mesh          : %u\n", settings.broker.mesh_enabled);
    print_back_to_menu(write_fn, ctx);
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

    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx, "Runtime\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Network       : %s\n", status.network_state);
    pc_write(write_fn, ctx, "IP            : %s\n", status.ip_addr);
    pc_write(write_fn, ctx, "Broker        : %s\n", status.broker_state);
    pc_write(write_fn, ctx, "Bridge        : %s\n", status.p2p_role);
    pc_write(write_fn, ctx, "Peers         : %u\n", status.connected_peers);
    pc_write(write_fn, ctx, "Remote subs   : %u\n", status.remote_subscriptions);
    pc_write(write_fn, ctx, "Error         : %s\n",
             status.last_error[0] ? status.last_error : "-");
    pc_write(write_fn, ctx, "\nConfig\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Device        : %s\n", settings.system.device_name);
    pc_write(write_fn, ctx, "Mode          : %s\n",
             product_config_network_mode_name(settings.network.mode));
    pc_write(write_fn, ctx, "ETH DHCP      : %u\n", settings.network.dhcp_enabled);
    pc_write(write_fn, ctx, "ETH IP        : %s\n", settings.network.device_ip);
    pc_write(write_fn, ctx, "ETH Gateway   : %s\n", settings.network.gateway);
    pc_write(write_fn, ctx, "ETH Netmask   : %s\n", settings.network.netmask);
    pc_write(write_fn, ctx, "ETH DNS       : %s\n", settings.network.dns);
    pc_write(write_fn, ctx, "Broker IP     : %s\n", settings.broker.broker_ip);
    pc_write(write_fn, ctx, "Broker port   : %u\n", settings.broker.mqtt_port);
    pc_write(write_fn, ctx, "Fallback port : %u\n", settings.broker.fallback_port);
    pc_write(write_fn, ctx, "Bridge port   : %u\n", settings.broker.p2p_port);
    pc_write(write_fn, ctx, "Broker        : %u\n", settings.broker.broker_enabled);
    pc_write(write_fn, ctx, "Bridge        : %u\n", settings.broker.bridge_enabled);
    pc_write(write_fn, ctx, "Mesh          : %u\n", settings.broker.mesh_enabled);
    print_back_to_menu(write_fn, ctx);
    return 0;
}

static void print_usage_ip(product_console_write_fn write_fn, void *ctx)
{
    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx, "Device Network Setting\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Command       : ip <addr> [gw] [mask]\n");
    pc_write(write_fn, ctx, "Example       : ip 10.88.0.2 10.88.0.1 255.255.255.0\n");
    pc_write(write_fn, ctx, "Effect        : save static IP; reboot to apply\n");
    print_back_to_menu(write_fn, ctx);
}

static void print_usage_mode(product_console_write_fn write_fn, void *ctx)
{
    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx, "Device Network Setting\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Command       : mode auto|eth\n");
    pc_write(write_fn, ctx, "Auto          : Ethernet with configured defaults\n");
    pc_write(write_fn, ctx, "ETH           : Ethernet only; web UI/API allowed\n");
    pc_write(write_fn, ctx, "Effect        : save network mode; reboot to apply\n");
    print_back_to_menu(write_fn, ctx);
}

static void print_usage_broker_port(product_console_write_fn write_fn, void *ctx)
{
    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx, "Local Broker Setting\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Command       : broker-port <port>\n");
    pc_write(write_fn, ctx, "Example       : broker-port 1883\n");
    pc_write(write_fn, ctx, "Meaning       : client connects to this broker port\n");
    pc_write(write_fn, ctx, "Effect        : save broker port; reboot to apply\n");
    print_back_to_menu(write_fn, ctx);
}

static void print_usage_broker_fallback_port(product_console_write_fn write_fn,
                                             void *ctx)
{
    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx, "Local Broker Setting\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Command       : broker-fallback-port <port>\n");
    pc_write(write_fn, ctx, "Example       : broker-fallback-port 1884\n");
    pc_write(write_fn, ctx, "Meaning       : alternate MQTT port for client fallback\n");
    pc_write(write_fn, ctx, "Effect        : save fallback port; reboot to apply\n");
    print_back_to_menu(write_fn, ctx);
}

static void print_usage_broker_ip(product_console_write_fn write_fn, void *ctx)
{
    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx, "Local Broker Setting\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Command       : broker-ip <ip>\n");
    pc_write(write_fn, ctx, "Example       : broker-ip 10.88.0.2\n");
    pc_write(write_fn, ctx, "Meaning       : local broker IP advertised/used by this node\n");
    pc_write(write_fn, ctx, "Effect        : save broker IP; reboot to apply\n");
    print_back_to_menu(write_fn, ctx);
}

static void print_usage_broker_state(product_console_write_fn write_fn, void *ctx)
{
    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx, "Local Broker Setting\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Command       : broker-state <0|1>\n");
    pc_write(write_fn, ctx, "Disable       : broker-state 0\n");
    pc_write(write_fn, ctx, "Enable        : broker-state 1\n");
    pc_write(write_fn, ctx, "Effect        : save broker state; reboot to apply\n");
    print_back_to_menu(write_fn, ctx);
}

static void print_usage_peer(product_console_write_fn write_fn, void *ctx)
{
    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx, "Peer Bridge Setting\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Command       : peer <i> <name> <host> [broker-port] [bridge-port] [0|1]\n");
    pc_write(write_fn, ctx, "Example       : peer 1 node2 10.88.0.3 1883 4884 1\n");
    pc_write(write_fn, ctx, "Index         : peer slot number\n");
    pc_write(write_fn, ctx, "Host          : peer broker IP or host name\n");
    pc_write(write_fn, ctx, "Effect        : save bridge peer\n");
    print_back_to_menu(write_fn, ctx);
}

static void print_usage_bridge_port(product_console_write_fn write_fn, void *ctx)
{
    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx, "Peer Bridge Setting\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Command       : bridge-port <port>\n");
    pc_write(write_fn, ctx, "Example       : bridge-port 4884\n");
    pc_write(write_fn, ctx, "Meaning       : local port used for broker-to-broker bridge traffic\n");
    pc_write(write_fn, ctx, "Effect        : save local bridge port; reboot to apply\n");
    print_back_to_menu(write_fn, ctx);
}

static void print_usage_defaults(product_console_write_fn write_fn, void *ctx)
{
    clear_screen(write_fn, ctx);
    pc_write(write_fn, ctx, "Peer Bridge Setting\n");
    pc_write(write_fn, ctx, "------------------------------\n");
    pc_write(write_fn, ctx, "Command       : defaults small|large\n");
    pc_write(write_fn, ctx, "Small profile : defaults small\n");
    pc_write(write_fn, ctx, "Large profile : defaults large\n");
    pc_write(write_fn, ctx, "Effect        : reset config profile; reboot to apply\n");
    print_back_to_menu(write_fn, ctx);
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
        copy_arg(settings.network.gateway, sizeof(settings.network.gateway),
                 "192.168.127.5");
        copy_arg(settings.network.dns, sizeof(settings.network.dns),
                 "192.168.127.5");
    }
    if (mask) {
        copy_arg(settings.network.netmask, sizeof(settings.network.netmask), mask);
    } else if (!settings.network.netmask[0]) {
        copy_arg(settings.network.netmask, sizeof(settings.network.netmask),
                 "255.255.0.0");
    }
    if (!settings.network.dns[0]) {
        copy_arg(settings.network.dns, sizeof(settings.network.dns),
                 settings.network.gateway[0] ? settings.network.gateway :
                 "192.168.127.5");
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

static int cmd_mode(char **save, product_console_write_fn write_fn, void *ctx)
{
    char *mode_name = next_token(save);
    field_bridge_settings_t settings;
    uint8_t mode;

    if (!mode_name ||
        product_config_network_mode_from_name(mode_name, &mode) != 0 ||
        product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR usage: mode auto|eth\n");
        return -1;
    }
    settings.network.mode = mode;
    if (product_config_set_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR save mode failed\n");
        return -1;
    }
    pc_write(write_fn, ctx, "OK saved mode=%s; reboot to apply\n",
             product_config_network_mode_name(settings.network.mode));
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

static int cmd_broker(char **save, product_console_write_fn write_fn, void *ctx)
{
    char *mqtt = next_token(save);
    char *p2p = next_token(save);
    char *ip = next_token(save);
    field_bridge_settings_t settings;

    if (!mqtt || !p2p || product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx,
                 "ERR legacy usage: broker <broker-port> <bridge-port> [broker-ip]\n");
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

static int cmd_broker_port(char **save, product_console_write_fn write_fn,
                           void *ctx)
{
    char *port = next_token(save);
    field_bridge_settings_t settings;

    if (!port || product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR usage: broker-port <port>\n");
        return -1;
    }
    settings.broker.mqtt_port = (uint16_t)atoi(port);
    if (product_config_set_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR save failed\n");
        return -1;
    }
    pc_write(write_fn, ctx, "OK saved broker-port=%u; reboot to apply\n",
             settings.broker.mqtt_port);
    return 0;
}

static int cmd_broker_fallback_port(char **save,
                                    product_console_write_fn write_fn,
                                    void *ctx)
{
    char *port = next_token(save);
    field_bridge_settings_t settings;

    if (!port || product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR usage: broker-fallback-port <port>\n");
        return -1;
    }
    settings.broker.fallback_port = (uint16_t)atoi(port);
    if (product_config_set_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR save failed\n");
        return -1;
    }
    pc_write(write_fn, ctx, "OK saved broker-fallback-port=%u; reboot to apply\n",
             settings.broker.fallback_port);
    return 0;
}

static int cmd_broker_ip(char **save, product_console_write_fn write_fn,
                         void *ctx)
{
    char *ip = next_token(save);
    field_bridge_settings_t settings;

    if (!ip || product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR usage: broker-ip <ip>\n");
        return -1;
    }
    copy_arg(settings.broker.broker_ip, sizeof(settings.broker.broker_ip), ip);
    if (product_config_set_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR save failed\n");
        return -1;
    }
    pc_write(write_fn, ctx, "OK saved broker-ip=%s; reboot to apply\n",
             settings.broker.broker_ip);
    return 0;
}

static int cmd_bridge_port(char **save, product_console_write_fn write_fn,
                           void *ctx)
{
    char *port = next_token(save);
    field_bridge_settings_t settings;

    if (!port || product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR usage: bridge-port <port>\n");
        return -1;
    }
    settings.broker.p2p_port = (uint16_t)atoi(port);
    if (product_config_set_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR save failed\n");
        return -1;
    }
    pc_write(write_fn, ctx, "OK saved bridge-port=%u; reboot to apply\n",
             settings.broker.p2p_port);
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
    if (value == 1) {
        (void)product_runtime_set_broker_enabled(1);
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
                 "ERR usage: peer <i> <name> <host> [broker-port] [bridge-port] [0|1]\n");
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
    switch (current_menu) {
    case PRODUCT_CONSOLE_MENU_MAIN:
        switch (index) {
        case 1:
            print_info_menu(write_fn, write_ctx);
            return 0;
        case 2:
            print_settings_menu(write_fn, write_ctx);
            return 0;
        case 3:
            print_system_menu(write_fn, write_ctx);
            return 0;
        default:
            pc_write(write_fn, write_ctx, "ERR unknown menu index; try menu\n");
            return -1;
        }

    case PRODUCT_CONSOLE_MENU_INFO:
        switch (index) {
        case 1:
            return cmd_status(write_fn, write_ctx);
        case 2:
            return cmd_info(write_fn, write_ctx);
        case 3:
            return cmd_show(write_fn, write_ctx);
        default:
            pc_write(write_fn, write_ctx, "ERR unknown info index; try info\n");
            return -1;
        }

    case PRODUCT_CONSOLE_MENU_SETTINGS:
        switch (index) {
        case 1:
            print_network_menu(write_fn, write_ctx);
            return 0;
        case 2:
            print_broker_menu(write_fn, write_ctx);
            return 0;
        case 3:
            print_peer_bridge_menu(write_fn, write_ctx);
            return 0;
        default:
            pc_write(write_fn, write_ctx, "ERR unknown settings index; try settings\n");
            return -1;
        }

    case PRODUCT_CONSOLE_MENU_DEVICE_NETWORK:
        switch (index) {
        case 1:
            print_usage_mode(write_fn, write_ctx);
            return 0;
        case 2:
            print_usage_ip(write_fn, write_ctx);
            return 0;
        case 3:
            return cmd_dhcp(write_fn, write_ctx);
        default:
            pc_write(write_fn, write_ctx, "ERR unknown network index; try device-network\n");
            return -1;
        }

    case PRODUCT_CONSOLE_MENU_BROKER:
        switch (index) {
        case 1:
            print_usage_broker_port(write_fn, write_ctx);
            return 0;
        case 2:
            print_usage_broker_fallback_port(write_fn, write_ctx);
            return 0;
        case 3:
            print_usage_broker_ip(write_fn, write_ctx);
            return 0;
        case 4:
            print_usage_broker_state(write_fn, write_ctx);
            return 0;
        default:
            pc_write(write_fn, write_ctx, "ERR unknown local broker index; try local-broker\n");
            return -1;
        }

    case PRODUCT_CONSOLE_MENU_PEER_BRIDGE:
        switch (index) {
        case 1:
            print_usage_bridge_port(write_fn, write_ctx);
            return 0;
        case 2:
            print_usage_peer(write_fn, write_ctx);
            return 0;
        case 3:
            print_usage_defaults(write_fn, write_ctx);
            return 0;
        default:
            pc_write(write_fn, write_ctx, "ERR unknown peer index; try peer-bridge\n");
            return -1;
        }

    case PRODUCT_CONSOLE_MENU_SYSTEM:
        switch (index) {
        case 1:
            if (product_config_reset_all() != 0) {
                pc_write(write_fn, write_ctx, "ERR reset failed\n");
                return -1;
            }
            pc_write(write_fn, write_ctx, "OK reset defaults; reboot to apply\n");
            return 0;
        case 2:
            pc_write(write_fn, write_ctx, "OK rebooting\n");
            if (reboot_fn) {
                reboot_fn(reboot_ctx);
            }
            return 0;
        default:
            pc_write(write_fn, write_ctx, "ERR unknown system index; try system\n");
            return -1;
        }

    default:
        current_menu = PRODUCT_CONSOLE_MENU_MAIN;
        pc_write(write_fn, write_ctx, "ERR menu state reset; try menu\n");
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
    if (strcmp(cmd, "0") == 0 || strcmp(cmd, "back") == 0) {
        print_menu(write_fn, write_ctx);
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
    if (strcmp(cmd, "info") == 0) {
        print_info_menu(write_fn, write_ctx);
        return 0;
    }
    if (strcmp(cmd, "settings") == 0) {
        print_settings_menu(write_fn, write_ctx);
        return 0;
    }
    if (strcmp(cmd, "device-network") == 0 || strcmp(cmd, "network") == 0) {
        print_network_menu(write_fn, write_ctx);
        return 0;
    }
    if (strcmp(cmd, "local-broker") == 0 ||
        strcmp(cmd, "broker-setting") == 0 ||
        strcmp(cmd, "broker-ui") == 0) {
        print_broker_menu(write_fn, write_ctx);
        return 0;
    }
    if (strcmp(cmd, "peer-bridge") == 0 || strcmp(cmd, "peer-ui") == 0) {
        print_peer_bridge_menu(write_fn, write_ctx);
        return 0;
    }
    if (strcmp(cmd, "system") == 0) {
        print_system_menu(write_fn, write_ctx);
        return 0;
    }
    if (strcmp(cmd, "status") == 0) {
        return cmd_status(write_fn, write_ctx);
    }
    if (strcmp(cmd, "summary") == 0) {
        return cmd_info(write_fn, write_ctx);
    }
    if (strcmp(cmd, "show") == 0) {
        return cmd_show(write_fn, write_ctx);
    }
    if (strcmp(cmd, "ip") == 0) {
        return cmd_ip(&save, write_fn, write_ctx);
    }
    if (strcmp(cmd, "mode") == 0) {
        return cmd_mode(&save, write_fn, write_ctx);
    }
    if (strcmp(cmd, "dhcp") == 0) {
        return cmd_dhcp(write_fn, write_ctx);
    }
    if (strcmp(cmd, "broker") == 0) {
        return cmd_broker(&save, write_fn, write_ctx);
    }
    if (strcmp(cmd, "broker-port") == 0) {
        return cmd_broker_port(&save, write_fn, write_ctx);
    }
    if (strcmp(cmd, "broker-fallback-port") == 0 ||
        strcmp(cmd, "fallback-port") == 0) {
        return cmd_broker_fallback_port(&save, write_fn, write_ctx);
    }
    if (strcmp(cmd, "broker-ip") == 0) {
        return cmd_broker_ip(&save, write_fn, write_ctx);
    }
    if (strcmp(cmd, "bridge-port") == 0) {
        return cmd_bridge_port(&save, write_fn, write_ctx);
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
#if defined(CONFIG_ETH_W5500) && CONFIG_ETH_W5500
    printk("\nfield-bridge console ready. type menu\n");
#else
    print_menu(zephyr_console_write, NULL);
#endif
    while (1) {
        char *line = console_getline();
        char line_buf[CONFIG_CONSOLE_INPUT_MAX_LINE_LEN + 1];

        if (line == NULL || line[0] == '\0') {
            continue;
        }
        snprintf(line_buf, sizeof(line_buf), "%s", line);
        (void)product_console_handle_line(line_buf,
                                          zephyr_console_write,
                                          NULL,
                                          zephyr_console_reboot,
                                          NULL);
    }
}

#define PRODUCT_CONSOLE_STACK_SIZE 4096

K_THREAD_STACK_DEFINE(console_stack, PRODUCT_CONSOLE_STACK_SIZE);
static struct k_thread console_thread_data;
static int console_started;

void product_console_start(void)
{
    if (console_started) {
        return;
    }
    console_getline_init();
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
