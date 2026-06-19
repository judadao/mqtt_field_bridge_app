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

static int persist_save(int idx)
{
    (void)idx;
    FILE *f = fopen(peers_file_path(), "wb");
    if (!f) return 0;
    (void)fwrite(peers, sizeof(field_bridge_peer_t), FIELD_BRIDGE_PEER_MAX, f);
    fclose(f);
    return 0;
}

#else  /* __ZEPHYR__ */

#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>

#define NVS_SECTOR_SIZE  DT_PROP(DT_CHOSEN(zephyr_flash), erase_block_size)
#define NVS_SECTOR_COUNT 2
#define NVS_PARTITION    storage_partition
#define PEER_KEY_BASE    1

static struct nvs_fs nvs;
static int nvs_ready;
static K_MUTEX_DEFINE(nvs_init_lock);

static int nvs_init_once(void)
{
    k_mutex_lock(&nvs_init_lock, K_FOREVER);
    if (nvs_ready) {
        k_mutex_unlock(&nvs_init_lock);
        return 0;
    }
    nvs.flash_device = FIXED_PARTITION_DEVICE(NVS_PARTITION);
    if (!device_is_ready(nvs.flash_device)) {
        k_mutex_unlock(&nvs_init_lock);
        return -ENODEV;
    }
    nvs.sector_size  = NVS_SECTOR_SIZE;
    nvs.sector_count = NVS_SECTOR_COUNT;
    nvs.offset       = FIXED_PARTITION_OFFSET(NVS_PARTITION);
    int rc = nvs_mount(&nvs);
    if (rc == 0) nvs_ready = 1;
    k_mutex_unlock(&nvs_init_lock);
    return rc;
}

static void persist_load(void)
{
    if (nvs_init_once() < 0) return;
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        ssize_t n = nvs_read(&nvs, (uint16_t)(PEER_KEY_BASE + i),
                             &peers[i], sizeof(peers[i]));
        if (n != (ssize_t)sizeof(peers[i])) {
            memset(&peers[i], 0, sizeof(peers[i]));
        }
    }
}

static int persist_save(int idx)
{
    if (nvs_init_once() < 0) return -ENODEV;
    ssize_t rc = nvs_write(&nvs, (uint16_t)(PEER_KEY_BASE + idx),
                           &peers[idx], sizeof(peers[idx]));
    if (rc < 0) {
        LOG_ERR("nvs_write peer %d failed: %d", idx, (int)rc);
        return (int)rc;
    }
    return 0;
}

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
    return persist_save(index);
}
