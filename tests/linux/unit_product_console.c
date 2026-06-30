/*
 * unit_product_console - UART console command parser tests.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../app/src/product_config.h"
#include "../../app/src/product_console.h"
#include "../../app/src/product_runtime.h"

static int tests_run;
static int tests_passed;
static int tests_failed;

#define CHECK(expr) do {                                                \
    tests_run++;                                                        \
    if (expr) {                                                         \
        tests_passed++;                                                 \
    } else {                                                            \
        tests_failed++;                                                 \
        fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);\
    }                                                                   \
} while (0)

#define RUN(fn) do {                                                    \
    printf("  %-50s ", #fn);                                            \
    before_each();                                                      \
    fn();                                                               \
    printf("%s\n", tests_failed == 0 ? "ok" : "FAIL");                 \
} while (0)

static char out_buf[4096];
static int reboot_called;

static void write_cb(void *ctx, const char *text)
{
    (void)ctx;
    strncat(out_buf, text, sizeof(out_buf) - strlen(out_buf) - 1);
}

static void reboot_cb(void *ctx)
{
    (void)ctx;
    reboot_called++;
}

static int run_cmd(const char *cmd)
{
    char line[256];

    snprintf(line, sizeof(line), "%s", cmd);
    out_buf[0] = '\0';
    return product_console_handle_line(line, write_cb, NULL, reboot_cb, NULL);
}

static void before_each(void)
{
    system("rm -rf /tmp/unit_console_config");
    setenv("DEPHY_CONFIG_DIR", "/tmp/unit_console_config", 1);
    out_buf[0] = '\0';
    reboot_called = 0;
    product_config_init();
    product_runtime_init();
}

static void test_help_and_status(void)
{
    CHECK(run_cmd("help") == 0);
    CHECK(strstr(out_buf, "commands:") != NULL);
    CHECK(strstr(out_buf, "menu") != NULL);
    CHECK(strstr(out_buf, "settings") != NULL);
    CHECK(strstr(out_buf, "summary") != NULL);
    CHECK(strstr(out_buf, "device-network") != NULL);
    CHECK(strstr(out_buf, "local-broker") != NULL);
    CHECK(strstr(out_buf, "mode auto|eth") != NULL);
    CHECK(strstr(out_buf, "wifi <ssid>") == NULL);
    CHECK(strstr(out_buf, "ap ") == NULL);
    CHECK(run_cmd("status") == 0);
    CHECK(strstr(out_buf, "Status") != NULL);
    CHECK(strstr(out_buf, "Network       : init") != NULL);
}

static void test_menu_lists_numbered_commands(void)
{
    CHECK(run_cmd("menu") == 0);
    CHECK(strstr(out_buf, "Field Bridge CLI menu") != NULL);
    CHECK(strstr(out_buf, "1. info") != NULL);
    CHECK(strstr(out_buf, "2. settings") != NULL);
    CHECK(strstr(out_buf, "3. system") != NULL);
    CHECK(strstr(out_buf, "Type a number to enter") != NULL);
    CHECK(run_cmd("0") == 0);
    CHECK(strstr(out_buf, "Field Bridge CLI menu") != NULL);
    CHECK(run_cmd("back") == 0);
    CHECK(strstr(out_buf, "Field Bridge CLI menu") != NULL);
}

static void test_menu_index_commands(void)
{
    CHECK(run_cmd("menu") == 0);
    CHECK(run_cmd("1") == 0);
    CHECK(strstr(out_buf, "Info") != NULL);
    CHECK(strstr(out_buf, "1. status") != NULL);
    CHECK(strstr(out_buf, "2. summary") != NULL);
    CHECK(run_cmd("1") == 0);
    CHECK(strstr(out_buf, "Network       : init") != NULL);
    CHECK(strstr(out_buf, "0. return to menu") != NULL);
    CHECK(run_cmd("2") == 0);
    CHECK(strstr(out_buf, "Runtime") != NULL);
    CHECK(strstr(out_buf, "0. return to menu") != NULL);
    CHECK(run_cmd("3") == 0);
    CHECK(strstr(out_buf, "Device        : esp32-min-broker") != NULL);
    CHECK(strstr(out_buf, "0. return to menu") != NULL);
    CHECK(run_cmd("0") == 0);
    CHECK(run_cmd("2") == 0);
    CHECK(strstr(out_buf, "Settings") != NULL);
    CHECK(strstr(out_buf, "1. device-network") != NULL);
    CHECK(strstr(out_buf, "2. local-broker") != NULL);
    CHECK(strstr(out_buf, "3. peer-bridge") != NULL);
    CHECK(run_cmd("1") == 0);
    CHECK(strstr(out_buf, "Device Network Setting") != NULL);
    CHECK(run_cmd("1") == 0);
    CHECK(strstr(out_buf, "Command       : mode auto|eth") != NULL);
    CHECK(run_cmd("2") == 0);
    CHECK(strstr(out_buf, "Command       : ip <addr> [gw] [mask]") != NULL);
    CHECK(run_cmd("0") == 0);
    CHECK(run_cmd("2") == 0);
    CHECK(run_cmd("2") == 0);
    CHECK(strstr(out_buf, "Local Broker Setting") != NULL);
    CHECK(strstr(out_buf, "1. broker-port") != NULL);
    CHECK(strstr(out_buf, "2. broker-ip") != NULL);
    CHECK(run_cmd("1") == 0);
    CHECK(strstr(out_buf, "Command       : broker-port <port>") != NULL);
    CHECK(strstr(out_buf, "Example       : broker-port 1883") != NULL);
    CHECK(run_cmd("99") != 0);
    CHECK(strstr(out_buf, "ERR unknown local broker index") != NULL);
    CHECK(run_cmd("0") == 0);
    CHECK(run_cmd("2") == 0);
    CHECK(run_cmd("3") == 0);
    CHECK(strstr(out_buf, "Peer Bridge Setting") != NULL);
    CHECK(strstr(out_buf, "1. bridge-port") != NULL);
    CHECK(strstr(out_buf, "2. peer") != NULL);
    CHECK(run_cmd("1") == 0);
    CHECK(strstr(out_buf, "Command       : bridge-port <port>") != NULL);
}

static void test_broker_save_config(void)
{
    field_bridge_settings_t settings;

    CHECK(run_cmd("broker 1884 4885 192.168.127.15") == 0);
    CHECK(product_config_get_settings(&settings) == 0);
    CHECK(settings.broker.mqtt_port == 1884);
    CHECK(settings.broker.p2p_port == 4885);
    CHECK(strcmp(settings.broker.broker_ip, "192.168.127.15") == 0);

    CHECK(run_cmd("broker-port 1886") == 0);
    CHECK(product_config_get_settings(&settings) == 0);
    CHECK(settings.broker.mqtt_port == 1886);

    CHECK(run_cmd("broker-ip 192.168.127.16") == 0);
    CHECK(product_config_get_settings(&settings) == 0);
    CHECK(strcmp(settings.broker.broker_ip, "192.168.127.16") == 0);

    CHECK(run_cmd("bridge-port 4886") == 0);
    CHECK(product_config_get_settings(&settings) == 0);
    CHECK(settings.broker.p2p_port == 4886);
}

static void test_peer_command_saves_peer(void)
{
    field_bridge_peer_t peer;

    CHECK(run_cmd("peer 1 node2 10.78.0.23 1883 4884 1") == 0);
    CHECK(product_config_get_peer(1, &peer) == 0);
    CHECK(strcmp(peer.name, "node2") == 0);
    CHECK(strcmp(peer.host, "10.78.0.23") == 0);
    CHECK(peer.mqtt_port == 1883);
    CHECK(peer.p2p_port == 4884);
    CHECK(peer.enabled == 1);
}

static void test_scan_and_show(void)
{
    CHECK(run_cmd("show") == 0);
    CHECK(strstr(out_buf, "Saved config") != NULL);
    CHECK(strstr(out_buf, "Device        : esp32-min-broker") != NULL);
    CHECK(strstr(out_buf, "Mode          : auto") != NULL);
    CHECK(strstr(out_buf, "ETH IP        : 192.168.127.10") != NULL);
    CHECK(strstr(out_buf, "WiFi") == NULL);
    CHECK(strstr(out_buf, "Broker IP     : 192.168.127.10") != NULL);
    CHECK(strstr(out_buf, "Broker port   :") != NULL);
    CHECK(strstr(out_buf, "Bridge port   :") != NULL);
    CHECK(run_cmd("summary") == 0);
    CHECK(strstr(out_buf, "Runtime") != NULL);
    CHECK(strstr(out_buf, "Config") != NULL);
}

static void test_ip_and_dhcp_commands_save_network_config(void)
{
    field_bridge_settings_t settings;

    CHECK(run_cmd("ip 192.168.127.4 192.168.127.5 255.255.0.0") == 0);
    CHECK(strstr(out_buf, "OK saved static ip=192.168.127.4") != NULL);
    CHECK(product_config_get_settings(&settings) == 0);
    CHECK(strcmp(settings.network.device_ip, "192.168.127.4") == 0);
    CHECK(strcmp(settings.network.gateway, "192.168.127.5") == 0);
    CHECK(strcmp(settings.network.netmask, "255.255.0.0") == 0);
    CHECK(strcmp(settings.network.dns, "192.168.127.5") == 0);
    CHECK(settings.network.dhcp_enabled == 0);

    CHECK(run_cmd("dhcp") == 0);
    CHECK(strstr(out_buf, "OK saved DHCP enabled") != NULL);
    CHECK(product_config_get_settings(&settings) == 0);
    CHECK(settings.network.dhcp_enabled == 1);
}

static void test_mode_commands_save_network_config(void)
{
    field_bridge_settings_t settings;

    CHECK(run_cmd("mode wifi") != 0);
    CHECK(strstr(out_buf, "ERR usage: mode auto|eth") != NULL);

    CHECK(run_cmd("mode eth") == 0);
    CHECK(product_config_get_settings(&settings) == 0);
    CHECK(settings.network.mode == FIELD_BRIDGE_NETWORK_MODE_ETH);

    CHECK(run_cmd("mode bad") != 0);
    CHECK(strstr(out_buf, "ERR usage: mode auto|eth") != NULL);
}

static void test_reset_defaults_and_reboot(void)
{
    field_bridge_settings_t settings;

    CHECK(run_cmd("ip 192.168.10.2 192.168.10.1 255.255.255.0") == 0);
    CHECK(run_cmd("reset") == 0);
    CHECK(product_config_get_settings(&settings) == 0);
    CHECK(strcmp(settings.network.device_ip, "192.168.127.10") == 0);
    CHECK(run_cmd("reboot") == 0);
    CHECK(reboot_called == 1);
}

static void test_rejects_bad_commands(void)
{
    CHECK(run_cmd("wifi onlyssid") != 0);
    CHECK(strstr(out_buf, "ERR unknown") != NULL);
    CHECK(run_cmd("scan") != 0);
    CHECK(strstr(out_buf, "ERR unknown") != NULL);
    CHECK(run_cmd("nope") != 0);
    CHECK(strstr(out_buf, "ERR unknown") != NULL);
}

int main(void)
{
    printf("=== unit_product_console ===\n");
    RUN(test_help_and_status);
    RUN(test_menu_lists_numbered_commands);
    RUN(test_menu_index_commands);
    RUN(test_broker_save_config);
    RUN(test_peer_command_saves_peer);
    RUN(test_scan_and_show);
    RUN(test_ip_and_dhcp_commands_save_network_config);
    RUN(test_mode_commands_save_network_config);
    RUN(test_reset_defaults_and_reboot);
    RUN(test_rejects_bad_commands);

    printf("%d/%d console checks passed\n", tests_passed, tests_run);
    return tests_failed ? 1 : 0;
}
