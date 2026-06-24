/*
 * run_web_config_server - host runner for manually inspecting provisioning UI.
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../app/src/provisioning_http.h"
#include "../../app/src/product_config.h"
#include "../../app/src/product_runtime.h"

static volatile sig_atomic_t keep_running = 1;

static void stop_running(int sig)
{
    (void)sig;
    keep_running = 0;
}

static const char *env_or_default(const char *name, const char *fallback)
{
    const char *value = getenv(name);
    return (value && value[0]) ? value : fallback;
}

static void copy_field(char *dst, size_t cap, const char *src)
{
    if (!cap) {
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

int main(void)
{
    field_bridge_settings_t settings;

    setenv("BRIDGE_PEERS_FILE", "out/web_peers.bin", 0);
    setenv("BRIDGE_SETTINGS_FILE", "out/web_settings.bin", 0);

    product_config_init();
    product_runtime_init();

    if (product_config_get_settings(&settings) != 0) {
        fprintf(stderr, "failed to load default settings\n");
        return 1;
    }

    copy_field(settings.network.device_ip, sizeof(settings.network.device_ip),
               env_or_default("WEB_TEST_DEVICE_IP", settings.network.device_ip));
    copy_field(settings.network.gateway, sizeof(settings.network.gateway),
               env_or_default("WEB_TEST_GATEWAY", settings.network.gateway));
    copy_field(settings.network.netmask, sizeof(settings.network.netmask),
               env_or_default("WEB_TEST_NETMASK", settings.network.netmask));
    copy_field(settings.network.dns, sizeof(settings.network.dns),
               env_or_default("WEB_TEST_DNS", settings.network.dns));
    settings.network.dhcp_enabled =
        (uint8_t)atoi(env_or_default("WEB_TEST_DHCP_ENABLED", "0"));

    if (product_config_set_settings(&settings) != 0 ||
        product_config_get_settings(&settings) != 0) {
        fprintf(stderr, "failed to apply web network settings\n");
        return 1;
    }

    product_runtime_network_start(&settings);
    provisioning_http_start();

    signal(SIGINT, stop_running);
    signal(SIGTERM, stop_running);

    printf("Provisioning web server listening on http://127.0.0.1:%d/\n",
           PROVISIONING_HTTP_PORT);
    printf("Network config: ip=%s gateway=%s netmask=%s dns=%s dhcp=%u\n",
           settings.network.device_ip,
           settings.network.gateway,
           settings.network.netmask,
           settings.network.dns,
           settings.network.dhcp_enabled);
    fflush(stdout);

    while (keep_running) {
        sleep(1);
    }
    return 0;
}
