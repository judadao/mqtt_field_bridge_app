#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "broker.h"
#include "client.h"
#include "p2p.h"

#include "bridge_control.h"
#include "product_config.h"
#include "provisioning_http.h"

LOG_MODULE_REGISTER(field_bridge_main, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("MQTT field bridge app starting");

    product_config_init();
    bridge_control_init();
    provisioning_http_start();

    client_pool_init();
    if (broker_init() != 0) {
        LOG_ERR("broker_init failed");
        return -1;
    }

    p2p_start();
    broker_run();
    return 0;
}
