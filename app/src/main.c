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
#include "product_ethernet.h"
#include "product_runtime.h"
#include "product_status_io.h"

LOG_MODULE_REGISTER(field_bridge_main, LOG_LEVEL_INF);

#define POWER_ON_SETTLE_MS              1500
#define W5500_LINK_TIMEOUT_MS           15000
#define W5500_LINK_SAMPLE_MS            250
#define W5500_LINK_STABLE_SAMPLES       4
#define HTTP_TO_BROKER_DELAY_MS         1500
#define BROKER_TO_P2P_DELAY_MS          1000
#define NETWORK_REBOOT_DELAY_MS         10000
#define BROKER_STAGGER_STEP_MS          750
#define BROKER_STAGGER_BUCKETS          8
#define MESH_PEER_LOSS_GRACE_MS         90000
#define MESH_PEER_LOSS_REBOOT_MS        60000

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
    field_bridge_settings_t settings;
    if (product_config_get_settings(&settings) == 0 &&
        settings.broker.fallback_port != settings.broker.mqtt_port) {
        int rc = broker_start_mesh_ingress_listener(settings.broker.fallback_port);

        if (rc != 0) {
            LOG_WRN("fallback MQTT listener failed port=%u rc=%d",
                    settings.broker.fallback_port, rc);
        }
    }
    product_runtime_broker_started();

#if defined(CONFIG_MQTT_P2P_DYNAMIC)
#ifdef __ZEPHYR__
    k_sleep(K_MSEC(BROKER_TO_P2P_DELAY_MS));
#endif
    LOG_INF("p2p startup requested after broker_init success");
    p2p_start();
#else
    LOG_INF("p2p disabled by build config");
#endif

    broker_run();
}

static int broker_thread_started;

static int enabled_peer_count(void)
{
    int count = 0;

    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        field_bridge_peer_t peer;

        if (product_config_get_peer(i, &peer) == 0 && peer.enabled) {
            count++;
        }
    }
    return count;
}

static int startup_stagger_ms(const char *ip_addr)
{
    unsigned int a, b, c, d;

    if (!ip_addr || sscanf(ip_addr, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return 0;
    }
    return (int)((d % BROKER_STAGGER_BUCKETS) * BROKER_STAGGER_STEP_MS);
}

static int start_broker_service(const field_bridge_settings_t *settings)
{
    if (broker_thread_started) {
        return 0;
    }
    if (!settings) {
        product_runtime_broker_failed("settings unavailable");
        product_status_io_set_error();
        return -EINVAL;
    }
    LOG_INF("broker startup requested: ip=%s mqtt=%u p2p=%u bridge=%u mesh=%u",
            settings->broker.broker_ip,
            settings->broker.mqtt_port,
            settings->broker.p2p_port,
            settings->broker.bridge_enabled,
            settings->broker.mesh_enabled);
    if (broker_set_bind_host(settings->broker.broker_ip) != 0) {
        LOG_ERR("invalid broker bind ip: %s", settings->broker.broker_ip);
        product_runtime_broker_failed("invalid broker bind ip");
        product_status_io_set_error();
        return -EINVAL;
    }
    if (broker_set_listen_port(settings->broker.mqtt_port) != 0) {
        LOG_ERR("invalid broker listen port: %u", settings->broker.mqtt_port);
        product_runtime_broker_failed("invalid broker listen port");
        product_status_io_set_error();
        return -EINVAL;
    }
    bridge_control_apply_peers();
    broker_set_activity_callback(broker_activity, NULL);
    product_status_io_set_running(1);
    k_thread_create(&broker_run_thread,
                    broker_run_stack,
                    K_THREAD_STACK_SIZEOF(broker_run_stack),
                    broker_service_entry,
                    NULL, NULL, NULL,
                    6, 0, K_NO_WAIT);
    k_thread_name_set(&broker_run_thread, "mqtt_broker");
    broker_thread_started = 1;
    return 0;
}
#endif

#if defined(__ZEPHYR__) && defined(CONFIG_ETH_W5500) && CONFIG_ETH_W5500
static int wait_for_w5500_link_stable(int timeout_ms)
{
    int elapsed_ms = 0;
    int stable_samples = 0;

    while (elapsed_ms < timeout_ms) {
        if (product_ethernet_link_ready()) {
            stable_samples++;
            if (stable_samples >= W5500_LINK_STABLE_SAMPLES) {
                LOG_INF("W5500 link stable after %d ms", elapsed_ms);
                return 0;
            }
        } else {
            stable_samples = 0;
        }
        k_sleep(K_MSEC(W5500_LINK_SAMPLE_MS));
        elapsed_ms += W5500_LINK_SAMPLE_MS;
    }
    LOG_WRN("W5500 link not stable after %d ms",
            timeout_ms);
    return -ETIMEDOUT;
}

#endif

static int start_broker_network(const field_bridge_settings_t *settings,
                                const char *eth_ip_addr,
                                char *ip_addr,
                                size_t ip_addr_cap,
                                uint8_t *active_mode)
{
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

#ifdef __ZEPHYR__
    k_sleep(K_MSEC(POWER_ON_SETTLE_MS));
#endif

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
        if (eth_start_rc == 0 &&
            wait_for_w5500_link_stable(W5500_LINK_TIMEOUT_MS) == 0) {
#else
        if (eth_start_rc == 0 && product_ethernet_link_ready()) {
#endif
            eth_ready = 1;
            int http_rc = provisioning_http_start();

            if (http_rc != 0) {
                LOG_ERR("provisioning HTTP start failed: %d", http_rc);
            }
        } else {
#if defined(__ZEPHYR__) && defined(CONFIG_ETH_W5500) && CONFIG_ETH_W5500
            LOG_ERR("W5500 management link unavailable; rebooting after slow retry window");
            product_runtime_broker_failed("ethernet link unavailable");
            product_status_io_set_error();
            k_sleep(K_MSEC(NETWORK_REBOOT_DELAY_MS));
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
    LOG_INF("W5500 management web ready; staged broker auto-start");
    k_sleep(K_MSEC(HTTP_TO_BROKER_DELAY_MS));
    {
        int stagger_ms = startup_stagger_ms(settings.broker.broker_ip);

        if (stagger_ms > 0) {
            LOG_INF("broker startup stagger %d ms", stagger_ms);
            k_sleep(K_MSEC(stagger_ms));
        }
    }
    int64_t broker_started_ms = 0;
    int64_t peer_zero_since_ms = 0;
    while (1) {
        if (!broker_thread_started) {
            field_bridge_settings_t requested_settings;

            if (product_config_get_settings(&requested_settings) != 0) {
                product_runtime_broker_failed("settings unavailable");
                product_status_io_set_error();
            } else {
                if (start_broker_service(&requested_settings) == 0) {
                    broker_started_ms = k_uptime_get();
                    peer_zero_since_ms = 0;
                }
            }
        } else if (product_runtime_broker_start_requested()) {
            field_bridge_settings_t requested_settings;

            if (product_config_get_settings(&requested_settings) == 0) {
                (void)start_broker_service(&requested_settings);
            }
        } else if (settings.broker.mesh_enabled && enabled_peer_count() > 0 &&
                   broker_started_ms > 0 &&
                   k_uptime_get() - broker_started_ms > MESH_PEER_LOSS_GRACE_MS) {
            field_bridge_runtime_status_t status;

            if (product_runtime_get_status(&status) == 0) {
                if (status.connected_peers == 0) {
                    if (peer_zero_since_ms == 0) {
                        peer_zero_since_ms = k_uptime_get();
                        LOG_WRN("mesh peers all disconnected; starting recovery timer");
                    } else if (k_uptime_get() - peer_zero_since_ms >
                               MESH_PEER_LOSS_REBOOT_MS) {
                        LOG_ERR("mesh peers stayed disconnected; rebooting for W5500/P2P recovery");
                        product_status_io_set_error();
                        sys_reboot(SYS_REBOOT_COLD);
                    }
                } else {
                    peer_zero_since_ms = 0;
                }
            }
        }
        k_sleep(K_SECONDS(10));
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
    if (broker_set_listen_port(settings.broker.mqtt_port) != 0) {
        LOG_ERR("invalid broker listen port: %u", settings.broker.mqtt_port);
        product_runtime_broker_failed("invalid broker listen port");
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
    if (settings.broker.fallback_port != settings.broker.mqtt_port) {
        int rc = broker_start_mesh_ingress_listener(settings.broker.fallback_port);

        if (rc != 0) {
            LOG_WRN("fallback MQTT listener failed port=%u rc=%d",
                    settings.broker.fallback_port, rc);
        }
    }
    product_runtime_broker_started();
#if defined(CONFIG_MQTT_P2P_DYNAMIC)
    k_sleep(K_MSEC(BROKER_TO_P2P_DELAY_MS));
    LOG_INF("p2p startup requested after broker_init success");
    p2p_start();
#else
    LOG_INF("p2p disabled by build config");
#endif
    broker_run();
#endif
    return 0;
}
