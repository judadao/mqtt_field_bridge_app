#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "broker.h"
#include "client.h"
#include "p2p.h"

#include "bridge_control.h"
#include "product_config.h"
#include "product_runtime.h"
#include "provisioning_http.h"

LOG_MODULE_REGISTER(field_bridge_main, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("MQTT field bridge app starting");

    product_config_init();
    product_runtime_init();

    field_bridge_settings_t settings;
    if (product_config_get_settings(&settings) == 0) {
        LOG_INF("network startup requested: device=%s ip=%s dhcp=%u ssid=%s",
                settings.system.device_name,
                settings.network.device_ip,
                settings.network.dhcp_enabled,
                settings.network.wifi_ssid[0] ? settings.network.wifi_ssid : "(ap-only)");
        product_runtime_network_start(&settings);
    } else {
        product_runtime_broker_failed("settings unavailable");
        return -1;
    }

    bridge_control_init();
    provisioning_http_start();

    if (!settings.broker.broker_enabled) {
        LOG_INF("broker disabled by product config");
        product_runtime_set_broker_enabled(0);
        return 0;
    }

    LOG_INF("broker startup requested: mqtt=%u p2p=%u bridge=%u mesh=%u",
            settings.broker.mqtt_port,
            settings.broker.p2p_port,
            settings.broker.bridge_enabled,
            settings.broker.mesh_enabled);
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
    return 0;
}
