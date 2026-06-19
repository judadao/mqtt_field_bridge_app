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

    client_pool_init();
    if (broker_init() != 0) {
        LOG_ERR("broker_init failed");
        product_runtime_broker_failed("broker_init failed");
        return -1;
    }
    product_runtime_broker_started();

    p2p_start();
    broker_run();
    return 0;
}
