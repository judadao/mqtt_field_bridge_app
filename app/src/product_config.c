#include <string.h>

#include <zephyr/logging/log.h>

#include "product_config.h"

LOG_MODULE_REGISTER(product_config, LOG_LEVEL_INF);

static field_bridge_peer_t peers[FIELD_BRIDGE_PEER_MAX];
static field_bridge_settings_t settings;

static void settings_defaults(void)
{
    memset(&settings, 0, sizeof(settings));
    strncpy(settings.system.device_name, "esp32-min-broker",
            sizeof(settings.system.device_name) - 1);
    strncpy(settings.system.admin_password, "admin",
            sizeof(settings.system.admin_password) - 1);
    strncpy(settings.network.ap_ssid, "ESP32-Min-Broker",
            sizeof(settings.network.ap_ssid) - 1);
    strncpy(settings.network.ap_password, "12345678",
            sizeof(settings.network.ap_password) - 1);
    strncpy(settings.network.device_ip, "192.168.4.1",
            sizeof(settings.network.device_ip) - 1);
    strncpy(settings.network.gateway, "192.168.4.1",
            sizeof(settings.network.gateway) - 1);
    strncpy(settings.network.netmask, "255.255.255.0",
            sizeof(settings.network.netmask) - 1);
    strncpy(settings.network.dns, "192.168.4.1",
            sizeof(settings.network.dns) - 1);
    settings.network.dhcp_enabled = 1;
    strncpy(settings.broker.site_id, "field-a",
            sizeof(settings.broker.site_id) - 1);
    strncpy(settings.broker.topic_prefix, "site/field-a",
            sizeof(settings.broker.topic_prefix) - 1);
    settings.broker.mqtt_port = 1883;
    settings.broker.p2p_port = 4884;
    settings.broker.broker_enabled = 1;
    settings.broker.bridge_enabled = 1;
    settings.broker.mesh_enabled = 1;
}

static void settings_large_defaults(void)
{
    settings_defaults();
    strncpy(settings.system.device_name, "esp32-field-router",
            sizeof(settings.system.device_name) - 1);
    strncpy(settings.network.ap_ssid, "ESP32-Field-Router",
            sizeof(settings.network.ap_ssid) - 1);
    strncpy(settings.broker.site_id, "field-large",
            sizeof(settings.broker.site_id) - 1);
    strncpy(settings.broker.topic_prefix, "site/field-large",
            sizeof(settings.broker.topic_prefix) - 1);
}

static int valid_bool(uint8_t v)
{
    return v <= 1;
}

static int valid_port(uint16_t port)
{
    return port > 0;
}

static int valid_nonempty(const char *s)
{
    return s && s[0] != '\0';
}

static int validate_peer(const field_bridge_peer_t *peer)
{
    if (!peer || !valid_bool(peer->enabled)) {
        return -1;
    }
    if (peer->enabled &&
        (!valid_nonempty(peer->host) ||
         !valid_port(peer->mqtt_port) ||
         !valid_port(peer->p2p_port))) {
        return -1;
    }
    return 0;
}

static int validate_settings(const field_bridge_settings_t *cfg)
{
    if (!cfg) {
        return -1;
    }
    if (!valid_nonempty(cfg->system.device_name) ||
        !valid_nonempty(cfg->system.admin_password)) {
        return -1;
    }
    if (!valid_nonempty(cfg->network.ap_ssid) ||
        !valid_nonempty(cfg->network.ap_password) ||
        !valid_nonempty(cfg->network.device_ip) ||
        !valid_nonempty(cfg->network.gateway) ||
        !valid_nonempty(cfg->network.netmask) ||
        !valid_nonempty(cfg->network.dns) ||
        !valid_bool(cfg->network.dhcp_enabled)) {
        return -1;
    }
    if (!valid_nonempty(cfg->broker.site_id) ||
        !valid_nonempty(cfg->broker.topic_prefix) ||
        !valid_port(cfg->broker.mqtt_port) ||
        !valid_port(cfg->broker.p2p_port) ||
        !valid_bool(cfg->broker.broker_enabled) ||
        !valid_bool(cfg->broker.bridge_enabled) ||
        !valid_bool(cfg->broker.mesh_enabled)) {
        return -1;
    }
    return 0;
}

#ifndef __ZEPHYR__
#include <stdio.h>
#include <stdlib.h>

static const char *peers_file_path(void)
{
    const char *p = getenv("BRIDGE_PEERS_FILE");
    return p ? p : "/tmp/mqtt_bridge_peers.bin";
}

static const char *settings_file_path(void)
{
    const char *p = getenv("BRIDGE_SETTINGS_FILE");
    return p ? p : "/tmp/mqtt_bridge_settings.bin";
}

static void persist_load(void)
{
    FILE *f = fopen(peers_file_path(), "rb");
    if (f) {
        size_t n = fread(peers, sizeof(field_bridge_peer_t), FIELD_BRIDGE_PEER_MAX, f);
        if (n != (size_t)FIELD_BRIDGE_PEER_MAX) {
            memset(&peers[n], 0,
                   (FIELD_BRIDGE_PEER_MAX - (int)n) * sizeof(field_bridge_peer_t));
        }
        fclose(f);
    }

    f = fopen(settings_file_path(), "rb");
    if (f) {
        size_t n = fread(&settings, sizeof(settings), 1, f);
        if (n != 1 || validate_settings(&settings) != 0) {
            settings_defaults();
        }
        fclose(f);
    }
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

static int persist_save_settings(void)
{
    FILE *f = fopen(settings_file_path(), "wb");
    if (!f) return 0;
    (void)fwrite(&settings, sizeof(settings), 1, f);
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
#define SETTINGS_KEY     100

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
    ssize_t n = nvs_read(&nvs, SETTINGS_KEY, &settings, sizeof(settings));
    if (n != (ssize_t)sizeof(settings) || validate_settings(&settings) != 0) {
        settings_defaults();
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

static int persist_save_settings(void)
{
    if (nvs_init_once() < 0) return -ENODEV;
    ssize_t rc = nvs_write(&nvs, SETTINGS_KEY, &settings, sizeof(settings));
    if (rc < 0) {
        LOG_ERR("nvs_write settings failed: %d", (int)rc);
        return (int)rc;
    }
    return 0;
}

#endif /* __ZEPHYR__ */

void product_config_init(void)
{
    memset(peers, 0, sizeof(peers));
    settings_defaults();
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
    if (index < 0 || index >= FIELD_BRIDGE_PEER_MAX ||
        validate_peer(peer) != 0) {
        return -1;
    }

    peers[index] = *peer;
    return persist_save(index);
}

int product_config_get_settings(field_bridge_settings_t *out)
{
    if (!out) {
        return -1;
    }

    *out = settings;
    return 0;
}

int product_config_set_settings(const field_bridge_settings_t *new_settings)
{
    if (validate_settings(new_settings) != 0) {
        return -1;
    }

    settings = *new_settings;
    return persist_save_settings();
}

int product_config_check_admin_password(const char *password)
{
    if (!password) {
        return 0;
    }
    return strcmp(password, settings.system.admin_password) == 0;
}

int product_config_reset_all(void)
{
    memset(peers, 0, sizeof(peers));
    settings_defaults();
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        if (persist_save(i) != 0) {
            return -1;
        }
    }
    return persist_save_settings();
}

int product_config_apply_defaults(field_bridge_defaults_profile_t profile)
{
    memset(peers, 0, sizeof(peers));
    switch (profile) {
    case FIELD_BRIDGE_PROFILE_SMALL:
        settings_defaults();
        break;
    case FIELD_BRIDGE_PROFILE_LARGE:
        settings_large_defaults();
        break;
    default:
        return -1;
    }
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        if (persist_save(i) != 0) {
            return -1;
        }
    }
    return persist_save_settings();
}
