#include <string.h>

#if defined(__ZEPHYR__) && defined(CONFIG_HWINFO)
#include <zephyr/drivers/hwinfo.h>
#endif
#include <zephyr/logging/log.h>

#include <dephy_config/config_store.h>

#include "product_config.h"

LOG_MODULE_REGISTER(product_config, LOG_LEVEL_INF);

static field_bridge_peer_t peers[FIELD_BRIDGE_PEER_MAX];
static field_bridge_settings_t settings;
static uint8_t settings_loaded_from_store;

#if defined(__ZEPHYR__) && defined(CONFIG_HWINFO)
static void apply_known_esp32_default_ip(void)
{
    static const struct {
        uint8_t id_tail[3];
        const char *ip;
    } known_boards[] = {
        { { 0x60, 0x37, 0x28 }, "192.168.127.10" },
        { { 0x0c, 0x5d, 0xd8 }, "192.168.127.11" },
        { { 0x60, 0x39, 0xe8 }, "192.168.127.12" },
        { { 0x0c, 0x20, 0x90 }, "192.168.127.13" },
        { { 0x09, 0xc4, 0x04 }, "192.168.127.14" },
        { { 0x5a, 0xb4, 0x1c }, "192.168.127.15" },
    };
    uint8_t device_id[8];
    ssize_t len = hwinfo_get_device_id(device_id, sizeof(device_id));

    if (len < 3) {
        return;
    }

    for (size_t i = 0; i < sizeof(known_boards) / sizeof(known_boards[0]); i++) {
        const uint8_t *tail = &device_id[(size_t)len - 3];

        if (memcmp(tail, known_boards[i].id_tail, 3) != 0) {
            continue;
        }

        strncpy(settings.network.device_ip, known_boards[i].ip,
                sizeof(settings.network.device_ip) - 1);
        strncpy(settings.broker.broker_ip, known_boards[i].ip,
                sizeof(settings.broker.broker_ip) - 1);
        LOG_INF("known ESP32 default IP set to %s", known_boards[i].ip);
        return;
    }
}
#else
static void apply_known_esp32_default_ip(void)
{
}
#endif

static void settings_defaults(void)
{
    memset(&settings, 0, sizeof(settings));
    strncpy(settings.system.device_name, "esp32-min-broker",
            sizeof(settings.system.device_name) - 1);
    strncpy(settings.network.device_ip, "192.168.127.10",
            sizeof(settings.network.device_ip) - 1);
    strncpy(settings.network.gateway, "192.168.127.5",
            sizeof(settings.network.gateway) - 1);
    strncpy(settings.network.netmask, "255.255.0.0",
            sizeof(settings.network.netmask) - 1);
    strncpy(settings.network.dns, "192.168.127.5",
            sizeof(settings.network.dns) - 1);
    settings.network.dhcp_enabled = 0;
    strncpy(settings.broker.broker_ip, "192.168.127.10",
            sizeof(settings.broker.broker_ip) - 1);
    apply_known_esp32_default_ip();
    settings.network.mode = FIELD_BRIDGE_NETWORK_MODE_AUTO;
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
    strncpy(settings.broker.site_id, "field-large",
            sizeof(settings.broker.site_id) - 1);
    strncpy(settings.broker.topic_prefix, "site/field-large",
            sizeof(settings.broker.topic_prefix) - 1);
}

static int valid_bool(uint8_t v)
{
    return v <= 1;
}

static int valid_network_mode(uint8_t mode)
{
    return mode == FIELD_BRIDGE_NETWORK_MODE_AUTO ||
           mode == FIELD_BRIDGE_NETWORK_MODE_ETH;
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
    if (!valid_nonempty(cfg->system.device_name)) {
        return -1;
    }
    if (!valid_nonempty(cfg->network.device_ip) ||
        !valid_nonempty(cfg->network.gateway) ||
        !valid_nonempty(cfg->network.netmask) ||
        !valid_nonempty(cfg->network.dns) ||
        !valid_bool(cfg->network.dhcp_enabled) ||
        !valid_network_mode(cfg->network.mode)) {
        return -1;
    }
    if (!valid_nonempty(cfg->broker.broker_ip) ||
        !valid_nonempty(cfg->broker.site_id) ||
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

#define PEER_KEY_BASE 1
#define SETTINGS_KEY  100

static void persist_load(void)
{
    settings_loaded_from_store = 0;
    if (dephy_config_store_init() != 0) {
        return;
    }
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        if (dephy_config_store_load((dephy_config_key_t)(PEER_KEY_BASE + i),
                                    &peers[i], sizeof(peers[i])) != 0) {
            memset(&peers[i], 0, sizeof(peers[i]));
        }
    }
    if (dephy_config_store_load(SETTINGS_KEY, &settings, sizeof(settings)) == 0 &&
        validate_settings(&settings) == 0) {
        settings_loaded_from_store = 1;
    } else {
        settings_defaults();
    }
}

static int persist_save(int idx)
{
    return dephy_config_store_save((dephy_config_key_t)(PEER_KEY_BASE + idx),
                                   &peers[idx], sizeof(peers[idx]));
}

static int persist_save_settings(void)
{
    return dephy_config_store_save(SETTINGS_KEY, &settings, sizeof(settings));
}

void product_config_init(void)
{
    memset(peers, 0, sizeof(peers));
    settings_defaults();
    settings_loaded_from_store = 0;
    persist_load();
    LOG_INF("product config initialized");
}

int product_config_has_saved_settings(void)
{
    return settings_loaded_from_store ? 1 : 0;
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
    if (persist_save_settings() != 0) {
        return -1;
    }
    settings_loaded_from_store = 1;
    return 0;
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
    if (persist_save_settings() != 0) {
        return -1;
    }
    settings_loaded_from_store = 1;
    return 0;
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
    if (persist_save_settings() != 0) {
        return -1;
    }
    settings_loaded_from_store = 1;
    return 0;
}

const char *product_config_network_mode_name(uint8_t mode)
{
    switch (mode) {
    case FIELD_BRIDGE_NETWORK_MODE_AUTO:
        return "auto";
    case FIELD_BRIDGE_NETWORK_MODE_ETH:
        return "eth";
    default:
        return "unknown";
    }
}

int product_config_network_mode_from_name(const char *name, uint8_t *mode)
{
    if (!name || !mode) {
        return -1;
    }
    if (strcmp(name, "auto") == 0) {
        *mode = FIELD_BRIDGE_NETWORK_MODE_AUTO;
        return 0;
    }
    if (strcmp(name, "eth") == 0 || strcmp(name, "ethernet") == 0) {
        *mode = FIELD_BRIDGE_NETWORK_MODE_ETH;
        return 0;
    }
    return -1;
}
