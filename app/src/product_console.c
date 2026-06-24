#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "product_config.h"
#include "product_console.h"
#include "product_runtime.h"

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

static void print_help(product_console_write_fn write_fn, void *ctx)
{
    pc_write(write_fn, ctx,
                  "commands: help status info show ip dhcp broker peer defaults reset reboot\n");
    pc_write(write_fn, ctx,
                  "  info                     show runtime and saved network/broker config\n");
    pc_write(write_fn, ctx,
                  "  ip <addr> [gw] [mask]    save static network IP, reboot to apply\n");
    pc_write(write_fn, ctx,
                  "  dhcp                     enable DHCP, reboot to apply\n");
    pc_write(write_fn, ctx,
                  "  broker <mqtt> <p2p> [ip]  save local broker ports/ip\n");
    pc_write(write_fn, ctx,
                  "  peer <i> <name> <host> [mqtt] [p2p] [0|1]\n");
    pc_write(write_fn, ctx,
                  "  defaults small|large      reset config profile\n");
}

static int cmd_status(product_console_write_fn write_fn, void *ctx)
{
    field_bridge_runtime_status_t status;

    if (product_runtime_get_status(&status) != 0) {
        pc_write(write_fn, ctx, "ERR status unavailable\n");
        return -1;
    }
    pc_write(write_fn, ctx,
                  "OK network=%s ip=%s broker=%s p2p=%s peers=%u remote_subs=%u error=%s\n",
                  status.network_state,
                  status.ip_addr,
                  status.broker_state,
                  status.p2p_role,
                  status.connected_peers,
                  status.remote_subscriptions,
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
    pc_write(write_fn, ctx,
                  "OK device=%s ip=%s gw=%s mask=%s dns=%s dhcp=%u broker_ip=%s mqtt=%u p2p=%u bridge=%u mesh=%u\n",
                  settings.system.device_name,
                  settings.network.device_ip,
                  settings.network.gateway,
                  settings.network.netmask,
                  settings.network.dns,
                  settings.network.dhcp_enabled,
                  settings.broker.broker_ip,
                  settings.broker.mqtt_port,
                  settings.broker.p2p_port,
                  settings.broker.bridge_enabled,
                  settings.broker.mesh_enabled);
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

    pc_write(write_fn, ctx,
                  "OK runtime net=%s ip=%s broker=%s p2p=%s peers=%u remote_subs=%u error=%s\n",
                  status.network_state,
                  status.ip_addr,
                  status.broker_state,
                  status.p2p_role,
                  status.connected_peers,
                  status.remote_subscriptions,
                  status.last_error[0] ? status.last_error : "-");
    pc_write(write_fn, ctx,
                  "OK config device=%s dhcp=%u ip=%s gw=%s mask=%s dns=%s broker_ip=%s mqtt=%u p2p=%u broker=%u bridge=%u mesh=%u\n",
                  settings.system.device_name,
                  settings.network.dhcp_enabled,
                  settings.network.device_ip,
                  settings.network.gateway,
                  settings.network.netmask,
                  settings.network.dns,
                  settings.broker.broker_ip,
                  settings.broker.mqtt_port,
                  settings.broker.p2p_port,
                  settings.broker.broker_enabled,
                  settings.broker.bridge_enabled,
                  settings.broker.mesh_enabled);
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
        copy_arg(settings.network.gateway, sizeof(settings.network.gateway), "192.168.127.5");
        copy_arg(settings.network.dns, sizeof(settings.network.dns), "192.168.127.5");
    }
    if (mask) {
        copy_arg(settings.network.netmask, sizeof(settings.network.netmask), mask);
    } else if (!settings.network.netmask[0]) {
        copy_arg(settings.network.netmask, sizeof(settings.network.netmask), "255.255.0.0");
    }
    if (!settings.network.dns[0]) {
        copy_arg(settings.network.dns, sizeof(settings.network.dns),
                 settings.network.gateway[0] ? settings.network.gateway : "192.168.127.5");
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
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        print_help(write_fn, write_ctx);
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
    if (strcmp(cmd, "broker") == 0) {
        return cmd_broker(&save, write_fn, write_ctx);
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
    printk("%s", text);
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
