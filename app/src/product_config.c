#include <string.h>

#include <zephyr/logging/log.h>

#include "product_config.h"

LOG_MODULE_REGISTER(product_config, LOG_LEVEL_INF);

static field_bridge_peer_t peers[FIELD_BRIDGE_PEER_MAX];

#ifndef __ZEPHYR__
#include <stdio.h>
#include <stdlib.h>

static const char *peers_file_path(void)
{
    const char *p = getenv("BRIDGE_PEERS_FILE");
    return p ? p : "/tmp/mqtt_bridge_peers.bin";
}

static void persist_load(void)
{
    FILE *f = fopen(peers_file_path(), "rb");
    if (!f) return;
    size_t n = fread(peers, sizeof(field_bridge_peer_t), FIELD_BRIDGE_PEER_MAX, f);
    if (n != (size_t)FIELD_BRIDGE_PEER_MAX) {
        memset(&peers[n], 0,
               (FIELD_BRIDGE_PEER_MAX - (int)n) * sizeof(field_bridge_peer_t));
    }
    fclose(f);
}

static void persist_save(int idx)
{
    (void)idx;
    FILE *f = fopen(peers_file_path(), "wb");
    if (!f) return;
    (void)fwrite(peers, sizeof(field_bridge_peer_t), FIELD_BRIDGE_PEER_MAX, f);
    fclose(f);
}

#else  /* __ZEPHYR__ — NVS wired in Task 4 */

static void persist_load(void) {}
static void persist_save(int idx) { (void)idx; }

#endif /* __ZEPHYR__ */

void product_config_init(void)
{
    memset(peers, 0, sizeof(peers));
    persist_load();
    LOG_INF("product config initialized");
}

int product_config_peer_count(void)
{
    return FIELD_BRIDGE_PEER_MAX;
}

int product_config_get_peer(int index, field_bridge_peer_t *out)
{
    if (!out || index < 0 || index >= FIELD_BRIDGE_PEER_MAX) {
        return -1;
    }

    *out = peers[index];
    return 0;
}

int product_config_set_peer(int index, const field_bridge_peer_t *peer)
{
    if (!peer || index < 0 || index >= FIELD_BRIDGE_PEER_MAX) {
        return -1;
    }

    peers[index] = *peer;
    persist_save(index);
    return 0;
}
