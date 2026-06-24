#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "broker.h"
#include "client.h"
#include "p2p.h"

#include "bridge_control.h"
#include "product_console.h"
#include "product_config.h"
#include "product_ethernet.h"
#include "product_runtime.h"
#include "provisioning_http.h"

LOG_MODULE_REGISTER(field_bridge_main, LOG_LEVEL_INF);

#ifdef __ZEPHYR__
#define BROKER_RUN_STACK_SIZE 2048
K_THREAD_STACK_DEFINE(broker_run_stack, BROKER_RUN_STACK_SIZE);
static struct k_thread broker_run_thread;

static void broker_service_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    client_pool_init();
    if (broker_init() != 0) {
        LOG_ERR("broker_init failed");
        product_runtime_broker_failed("broker_init failed");
        return;
    }
    product_runtime_broker_started();

#if defined(CONFIG_MQTT_P2P_DYNAMIC)
    LOG_INF("p2p startup requested after broker_init success");
    p2p_start();
#else
    LOG_INF("p2p disabled by build config");
#endif

    broker_run();
}
#endif

int main(void)
{
    LOG_INF("MQTT field bridge app starting");

    product_config_init();
    product_runtime_init();

    field_bridge_settings_t settings;
    if (product_config_get_settings(&settings) == 0) {
        char ip_addr[FIELD_BRIDGE_HOST_MAX];
        char configured_ip[FIELD_BRIDGE_HOST_MAX];
        uint8_t requested_dhcp = settings.network.dhcp_enabled;

        snprintf(configured_ip, sizeof(configured_ip), "%s",
                 settings.network.device_ip);

        LOG_INF("network startup requested: device=%s ip=%s broker_ip=%s dhcp=%u transport=ethernet",
                settings.system.device_name,
                settings.network.device_ip,
                settings.broker.broker_ip,
                settings.network.dhcp_enabled);
        if (product_ethernet_start(&settings, ip_addr, sizeof(ip_addr)) != 0) {
            product_runtime_broker_failed("ethernet start failed");
            return -1;
        }
        if (ip_addr[0]) {
            snprintf(settings.network.device_ip, sizeof(settings.network.device_ip),
                     "%s", ip_addr);
        }
        if (requested_dhcp && configured_ip[0] &&
            strcmp(ip_addr, configured_ip) == 0) {
            settings.network.dhcp_enabled = 0;
        }
        product_runtime_network_start(&settings);
        if (!product_runtime_network_ready()) {
            product_runtime_broker_failed("network not ready");
            return -1;
        }
    } else {
        product_runtime_broker_failed("settings unavailable");
        return -1;
    }

    bridge_control_init();
    provisioning_http_start();
    product_console_start();

    if (!settings.broker.broker_enabled) {
        LOG_INF("broker disabled by product config");
        product_runtime_set_broker_enabled(0);
#ifdef __ZEPHYR__
        k_thread_priority_set(k_current_get(), 7);
        provisioning_http_run();
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
        return -1;
    }

#ifdef __ZEPHYR__
    k_thread_create(&broker_run_thread,
                    broker_run_stack,
                    K_THREAD_STACK_SIZEOF(broker_run_stack),
                    broker_service_entry,
                    NULL, NULL, NULL,
                    6, 0, K_NO_WAIT);
    k_thread_name_set(&broker_run_thread, "mqtt_broker");
    k_thread_priority_set(k_current_get(), 7);
    provisioning_http_run();
#else
    client_pool_init();
    if (broker_init() != 0) {
        LOG_ERR("broker_init failed");
        product_runtime_broker_failed("broker_init failed");
        return -1;
    }
    product_runtime_broker_started();
#if defined(CONFIG_MQTT_P2P_DYNAMIC)
    LOG_INF("p2p startup requested after broker_init success");
    p2p_start();
#else
    LOG_INF("p2p disabled by build config");
#endif
    broker_run();
#endif
    return 0;
}
