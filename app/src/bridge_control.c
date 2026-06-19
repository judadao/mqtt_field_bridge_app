#include <zephyr/logging/log.h>

#include "bridge_control.h"

LOG_MODULE_REGISTER(bridge_control, LOG_LEVEL_INF);

void bridge_control_init(void)
{
    LOG_INF("bridge control initialized");
}

void bridge_control_apply_peers(void)
{
    LOG_INF("bridge peer apply is not implemented yet");
}
