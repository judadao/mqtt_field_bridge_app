#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "product_config.h"
#include "product_console.h"
#include "product_runtime.h"
#include "product_wifi.h"

#ifdef __ZEPHYR__
#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(product_console, LOG_LEVEL_INF);
#endif

#define CONSOLE_SCAN_JSON_MAX 1024

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
                  "commands: help status show scan wifi clear-wifi ap broker peer defaults reset reboot\n");
    pc_write(write_fn, ctx,
                  "  wifi <ssid> <pass>        save/apply STA Wi-Fi\n");
    pc_write(write_fn, ctx,
                  "  clear-wifi                return to AP-only STA config\n");
    pc_write(write_fn, ctx,
                  "  ap <ssid> <pass>          save SoftAP config, reboot to apply\n");
    pc_write(write_fn, ctx,
                  "  broker <mqtt> <p2p>       save broker ports\n");
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
                  "OK wifi=%s ip=%s broker=%s p2p=%s peers=%u remote_subs=%u error=%s\n",
                  status.wifi_state,
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
    field_bridge_wifi_state_t bridge_wifi;

    if (product_config_get_settings(&settings) != 0 ||
        product_config_get_bridge_wifi(&bridge_wifi) != 0) {
        pc_write(write_fn, ctx, "ERR config unavailable\n");
        return -1;
    }
    pc_write(write_fn, ctx,
                  "OK device=%s ap=%s ap_ip=%s sta=%s dhcp=%u mqtt=%u p2p=%u bridge=%u mesh=%u\n",
                  settings.system.device_name,
                  settings.network.ap_ssid,
                  settings.network.device_ip,
                  settings.network.wifi_ssid[0] ? settings.network.wifi_ssid : "-",
                  settings.network.dhcp_enabled,
                  settings.broker.mqtt_port,
                  settings.broker.p2p_port,
                  settings.broker.bridge_enabled,
                  settings.broker.mesh_enabled);
    pc_write(write_fn, ctx,
                  "OK bridge_wifi enabled=%u connected=%u ssid=%s host=%s\n",
                  bridge_wifi.enabled,
                  bridge_wifi.connected,
                  bridge_wifi.current.ssid[0] ? bridge_wifi.current.ssid : "-",
                  bridge_wifi.current.host[0] ? bridge_wifi.current.host : "-");
    return 0;
}

static int save_and_apply_wifi(field_bridge_settings_t *settings,
                               product_console_write_fn write_fn,
                               void *ctx)
{
    if (product_config_set_settings(settings) != 0) {
        pc_write(write_fn, ctx, "ERR save failed\n");
        return -1;
    }
    if (product_wifi_apply_settings(settings) != 0) {
        pc_write(write_fn, ctx, "ERR apply failed\n");
        return -1;
    }
    product_runtime_network_start(settings);
    pc_write(write_fn, ctx, "OK saved wifi; STA reconnect requested\n");
    return 0;
}

static int cmd_wifi(char **save, product_console_write_fn write_fn, void *ctx)
{
    char *ssid = next_token(save);
    char *pass = next_token(save);
    field_bridge_settings_t settings;

    if (!ssid || !pass || product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR usage: wifi <ssid> <pass>\n");
        return -1;
    }
    copy_arg(settings.network.wifi_ssid, sizeof(settings.network.wifi_ssid), ssid);
    copy_arg(settings.network.wifi_password, sizeof(settings.network.wifi_password), pass);
    settings.network.dhcp_enabled = 1;
    return save_and_apply_wifi(&settings, write_fn, ctx);
}

static int cmd_clear_wifi(product_console_write_fn write_fn, void *ctx)
{
    field_bridge_settings_t settings;

    if (product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR config unavailable\n");
        return -1;
    }
    settings.network.wifi_ssid[0] = '\0';
    settings.network.wifi_password[0] = '\0';
    copy_arg(settings.network.device_ip, sizeof(settings.network.device_ip), "192.168.4.1");
    copy_arg(settings.network.gateway, sizeof(settings.network.gateway), "192.168.4.1");
    copy_arg(settings.network.dns, sizeof(settings.network.dns), "192.168.4.1");
    settings.network.dhcp_enabled = 1;
    if (product_config_set_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR save failed\n");
        return -1;
    }
    product_runtime_network_start(&settings);
    pc_write(write_fn, ctx, "OK cleared STA Wi-Fi; reboot if SoftAP is not visible\n");
    return 0;
}

static int cmd_ap(char **save, product_console_write_fn write_fn, void *ctx)
{
    char *ssid = next_token(save);
    char *pass = next_token(save);
    field_bridge_settings_t settings;

    if (!ssid || !pass || product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR usage: ap <ssid> <pass>\n");
        return -1;
    }
    copy_arg(settings.network.ap_ssid, sizeof(settings.network.ap_ssid), ssid);
    copy_arg(settings.network.ap_password, sizeof(settings.network.ap_password), pass);
    if (product_config_set_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR save failed\n");
        return -1;
    }
    pc_write(write_fn, ctx, "OK saved AP config; reboot to apply\n");
    return 0;
}

static int cmd_broker(char **save, product_console_write_fn write_fn, void *ctx)
{
    char *mqtt = next_token(save);
    char *p2p = next_token(save);
    field_bridge_settings_t settings;

    if (!mqtt || !p2p || product_config_get_settings(&settings) != 0) {
        pc_write(write_fn, ctx, "ERR usage: broker <mqtt> <p2p>\n");
        return -1;
    }
    settings.broker.mqtt_port = (uint16_t)atoi(mqtt);
    settings.broker.p2p_port = (uint16_t)atoi(p2p);
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

static int cmd_scan(product_console_write_fn write_fn, void *ctx)
{
    char json[CONSOLE_SCAN_JSON_MAX];
    int rc = product_wifi_scan_json(json, sizeof(json));

    if (rc != 0) {
        pc_write(write_fn, ctx, "ERR scan failed rc=%d\n", rc);
        return -1;
    }
    if (write_fn) {
        write_fn(ctx, "OK scan ");
        write_fn(ctx, json);
        write_fn(ctx, "\n");
    }
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
    if (strcmp(cmd, "scan") == 0) {
        return cmd_scan(write_fn, write_ctx);
    }
    if (strcmp(cmd, "wifi") == 0) {
        return cmd_wifi(&save, write_fn, write_ctx);
    }
    if (strcmp(cmd, "clear-wifi") == 0) {
        return cmd_clear_wifi(write_fn, write_ctx);
    }
    if (strcmp(cmd, "ap") == 0) {
        return cmd_ap(&save, write_fn, write_ctx);
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
