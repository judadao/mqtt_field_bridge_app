#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include "broker.h"
#include "client.h"
#include "p2p.h"

#include "bridge_control.h"
#include "provisioning_http.h"
#include "product_console.h"
#include "product_config.h"
#include "product_wifi.h"
#include "product_ethernet.h"
#include "product_runtime.h"
#include "product_status_io.h"

LOG_MODULE_REGISTER(field_bridge_main, LOG_LEVEL_INF);

#ifdef __ZEPHYR__
#define BROKER_RUN_STACK_SIZE 3072
K_THREAD_STACK_DEFINE(broker_run_stack, BROKER_RUN_STACK_SIZE);
static struct k_thread broker_run_thread;

static void reboot_later(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(status_button_reboot_work, reboot_later);

static void reboot_later(struct k_work *work)
{
    ARG_UNUSED(work);
    sys_reboot(SYS_REBOOT_COLD);
}

static void broker_activity(void *ctx)
{
    ARG_UNUSED(ctx);
    product_status_io_record_activity();
}

static void status_config_reset_press(void *ctx)
{
    ARG_UNUSED(ctx);

    if (product_config_reset_all() != 0) {
        LOG_ERR("power button config reset: config reset failed");
        return;
    }
    product_status_io_set_running(0);
    product_runtime_set_broker_enabled(0);
    LOG_INF("power button config reset: defaults restored, rebooting");
    k_work_schedule(&status_button_reboot_work, K_MSEC(500));
}

static void broker_service_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    client_pool_init();
    if (broker_init() != 0) {
        LOG_ERR("broker_init failed");
        product_runtime_broker_failed("broker_init failed");
        product_status_io_set_error();
        return;
    }
    product_runtime_broker_started();

#if defined(CONFIG_MQTT_P2P_DYNAMIC) && \
    !(defined(CONFIG_ETH_W5500) && CONFIG_ETH_W5500)
    LOG_INF("p2p startup requested after broker_init success");
    p2p_start();
#else
    LOG_INF("p2p disabled by build config");
#endif

    broker_run();
}
#endif

static int wifi_configured(const field_bridge_settings_t *settings)
{
    return settings && settings->network.wifi_ssid[0] != '\0';
}

#if defined(__ZEPHYR__) && defined(CONFIG_ETH_W5500) && CONFIG_ETH_W5500
static void wait_for_w5500_link_grace(int timeout_ms)
{
    int elapsed_ms = 0;

    while (elapsed_ms < timeout_ms) {
        if (product_ethernet_link_ready()) {
            LOG_INF("W5500 link ready after %d ms", elapsed_ms);
            return;
        }
        k_sleep(K_MSEC(250));
        elapsed_ms += 250;
    }
    LOG_WRN("W5500 link not confirmed after %d ms; starting management web anyway",
            timeout_ms);
}

#endif

static int start_broker_network(const field_bridge_settings_t *settings,
                                const char *eth_ip_addr,
                                char *ip_addr,
                                size_t ip_addr_cap,
                                uint8_t *active_mode)
{
    int rc;

    if (!settings || !eth_ip_addr || !ip_addr || ip_addr_cap == 0 || !active_mode) {
        return -EINVAL;
    }
    ip_addr[0] = '\0';
    *active_mode = settings->network.mode;

    switch (settings->network.mode) {
    case FIELD_BRIDGE_NETWORK_MODE_ETH:
        snprintf(ip_addr, ip_addr_cap, "%s", eth_ip_addr);
        *active_mode = FIELD_BRIDGE_NETWORK_MODE_ETH;
        return 0;
    case FIELD_BRIDGE_NETWORK_MODE_WIFI:
        if (!wifi_configured(settings)) {
            return -ENODATA;
        }
        rc = product_wifi_start(settings, ip_addr, ip_addr_cap);
        if (rc == 0) {
            *active_mode = FIELD_BRIDGE_NETWORK_MODE_WIFI;
        }
        return rc;
    case FIELD_BRIDGE_NETWORK_MODE_AUTO:
        snprintf(ip_addr, ip_addr_cap, "%s", eth_ip_addr);
        *active_mode = FIELD_BRIDGE_NETWORK_MODE_ETH;
        return 0;
    default:
        return -EINVAL;
    }
}

int main(void)
{
    LOG_INF("MQTT field bridge app starting");

    product_config_init();
    product_runtime_init();
    product_status_io_init(NULL, NULL, status_config_reset_press, NULL);
    product_console_start();

    field_bridge_settings_t settings;
    if (product_config_get_settings(&settings) == 0) {
        char ip_addr[FIELD_BRIDGE_HOST_MAX];
        char eth_ip_addr[FIELD_BRIDGE_HOST_MAX];
        char configured_ip[FIELD_BRIDGE_HOST_MAX];
        field_bridge_settings_t runtime_settings;
        uint8_t active_network_mode = settings.network.mode;
        uint8_t requested_dhcp = settings.network.dhcp_enabled;
        int eth_ready = 0;

        snprintf(configured_ip, sizeof(configured_ip), "%s",
                 settings.network.device_ip);

        LOG_INF("network startup requested: device=%s ip=%s broker_ip=%s dhcp=%u transport=%s",
                settings.system.device_name,
                settings.network.device_ip,
                settings.broker.broker_ip,
                settings.network.dhcp_enabled,
                product_config_network_mode_name(settings.network.mode));
        eth_ip_addr[0] = '\0';
        int eth_start_rc = product_ethernet_start(&settings, eth_ip_addr, sizeof(eth_ip_addr));

        LOG_INF("ethernet start result rc=%d ip=%s", eth_start_rc,
                eth_ip_addr[0] ? eth_ip_addr : "-");
#if defined(__ZEPHYR__) && defined(CONFIG_ETH_W5500) && CONFIG_ETH_W5500
        if (eth_start_rc == 0) {
            wait_for_w5500_link_grace(15000);
#else
        if (eth_start_rc == 0 && product_ethernet_link_ready()) {
#endif
            eth_ready = 1;
            int http_rc = provisioning_http_start();

            if (http_rc != 0) {
                LOG_ERR("provisioning HTTP start failed: %d", http_rc);
            }
        } else if (settings.network.mode != FIELD_BRIDGE_NETWORK_MODE_WIFI) {
#if defined(__ZEPHYR__) && defined(CONFIG_ETH_W5500) && CONFIG_ETH_W5500
            LOG_ERR("W5500 management link unavailable; rebooting to retry");
            k_sleep(K_SECONDS(3));
            sys_reboot(SYS_REBOOT_COLD);
#endif
            LOG_INF("ethernet management unavailable; waiting for UART CLI provisioning");
            product_runtime_broker_failed("UART provisioning required");
            product_status_io_set_running(0);
#ifdef __ZEPHYR__
            while (1) {
                k_sleep(K_SECONDS(60));
            }
#else
            return 0;
#endif
        }

        if (!eth_ready && settings.network.mode == FIELD_BRIDGE_NETWORK_MODE_WIFI) {
            LOG_INF("ethernet management unavailable; continuing WiFi mode without web");
        }
        if (start_broker_network(&settings, eth_ip_addr, ip_addr, sizeof(ip_addr),
                                 &active_network_mode) != 0) {
            product_runtime_broker_failed("network start failed");
            product_status_io_set_error();
            if (eth_ready) {
                LOG_INF("management web remains available on ethernet for reconfiguration");
#ifdef __ZEPHYR__
                k_thread_priority_set(k_current_get(), 7);
                while (1) {
                    k_sleep(K_SECONDS(60));
                }
#endif
            }
            return -1;
        }
        LOG_INF("network active transport=%s ip=%s",
                product_config_network_mode_name(active_network_mode),
                ip_addr[0] ? ip_addr : "-");
        runtime_settings = settings;
        if (ip_addr[0] && active_network_mode == FIELD_BRIDGE_NETWORK_MODE_ETH) {
            snprintf(runtime_settings.network.device_ip,
                     sizeof(runtime_settings.network.device_ip), "%s", ip_addr);
            if (runtime_settings.network.dhcp_enabled ||
                runtime_settings.broker.broker_ip[0] == '\0' ||
                strcmp(runtime_settings.broker.broker_ip, "0.0.0.0") == 0) {
                snprintf(runtime_settings.broker.broker_ip,
                         sizeof(runtime_settings.broker.broker_ip), "%s", ip_addr);
            }
        } else if (ip_addr[0] && active_network_mode == FIELD_BRIDGE_NETWORK_MODE_WIFI) {
            snprintf(runtime_settings.network.device_ip,
                     sizeof(runtime_settings.network.device_ip), "%s", ip_addr);
            runtime_settings.network.dhcp_enabled = settings.network.wifi_dhcp_enabled;
            snprintf(runtime_settings.broker.broker_ip,
                     sizeof(runtime_settings.broker.broker_ip), "%s", ip_addr);
        }
        if (requested_dhcp && configured_ip[0] &&
            strcmp(ip_addr, configured_ip) == 0) {
            runtime_settings.network.dhcp_enabled = 0;
        }
        runtime_settings.network.mode = active_network_mode;
        product_runtime_network_start(&runtime_settings);
        if (!product_runtime_network_ready()) {
            product_runtime_broker_failed("network not ready");
            product_status_io_set_error();
            return -1;
        }
        if (active_network_mode == FIELD_BRIDGE_NETWORK_MODE_ETH &&
            !runtime_settings.broker.broker_enabled) {
            runtime_settings.broker.broker_enabled = 1;
            (void)product_config_set_settings(&runtime_settings);
            LOG_INF("broker_enabled was disabled; enabling for power-on startup");
        }
        settings = runtime_settings;
    } else {
        product_runtime_broker_failed("settings unavailable");
        product_status_io_set_error();
        return -1;
    }

    bridge_control_init();

#if defined(CONFIG_ETH_W5500) && CONFIG_ETH_W5500
    LOG_INF("W5500 management web ready; broker auto-start deferred");
    product_runtime_set_broker_enabled(0);
    while (1) {
        k_sleep(K_SECONDS(60));
    }
#endif

    if (!settings.broker.broker_enabled) {
        LOG_INF("broker disabled by product config");
        product_runtime_set_broker_enabled(0);
        product_status_io_set_running(0);
#ifdef __ZEPHYR__
        k_thread_priority_set(k_current_get(), 7);
        while (1) {
            k_sleep(K_SECONDS(60));
        }
#endif
        return 0;
    }

    LOG_INF("broker startup requested: ip=%s mqtt=%u p2p=%u bridge=%u mesh=%u",
            settings.broker.broker_ip,
            settings.broker.mqtt_port,
            settings.broker.p2p_port,
            settings.broker.bridge_enabled,
            settings.broker.mesh_enabled);
    if (broker_set_bind_host(settings.broker.broker_ip) != 0) {
        LOG_ERR("invalid broker bind ip: %s", settings.broker.broker_ip);
        product_runtime_broker_failed("invalid broker bind ip");
        product_status_io_set_error();
        return -1;
    }
    broker_set_activity_callback(broker_activity, NULL);
    product_status_io_set_running(1);

#ifdef __ZEPHYR__
    k_thread_create(&broker_run_thread,
                    broker_run_stack,
                    K_THREAD_STACK_SIZEOF(broker_run_stack),
                    broker_service_entry,
                    NULL, NULL, NULL,
                    6, 0, K_NO_WAIT);
    k_thread_name_set(&broker_run_thread, "mqtt_broker");
    k_thread_priority_set(k_current_get(), 7);
    while (1) {
        k_sleep(K_SECONDS(60));
    }
#else
    client_pool_init();
    if (broker_init() != 0) {
        LOG_ERR("broker_init failed");
        product_runtime_broker_failed("broker_init failed");
        product_status_io_set_error();
        return -1;
    }
    product_runtime_broker_started();
#if defined(CONFIG_MQTT_P2P_DYNAMIC) && \
    !(defined(CONFIG_ETH_W5500) && CONFIG_ETH_W5500)
    LOG_INF("p2p startup requested after broker_init success");
    p2p_start();
#else
    LOG_INF("p2p disabled by build config");
#endif
    broker_run();
#endif
    return 0;
}
