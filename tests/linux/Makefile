ROOT    := ../..
BROKER  := $(ROOT)/deps/mqtt_min_broker
DEPHY_IO := $(ROOT)/deps/dephy_industrial_io
DEPHY_CONFIG := $(ROOT)/deps/dephy_config
DEPHY_CLI := $(ROOT)/deps/dephy_cli
APP_SRC := $(ROOT)/app/src

CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L \
         -I$(APP_SRC) \
         -Iinclude \
         -I$(BROKER)/include \
         -I$(DEPHY_IO)/include \
         -I$(DEPHY_CONFIG)/include \
         -I$(DEPHY_CLI)/include
OUTDIR = out

.PHONY: all test unit-tests unit_product_config unit_product_runtime \
        unit_product_topics unit_bridge_control \
        unit_product_console unit_provisioning_http \
        unit_product_io_bridge \
        integration-tests hardware-esp32-chain-bridge-test hardware-esp32-network-bind-test hardware-esp32-config-save-reboot-test stress scale-tests \
        testkit-wrapper testkit-unit-tests testkit-integration-tests testkit-stress \
        scale-ring-tests client-fallback-test clean broker-p2p

all: unit-tests

$(OUTDIR):
	mkdir -p $(OUTDIR)

# ── broker prerequisite ────────────────────────────────────────────────────
broker-p2p:
	$(MAKE) -C $(BROKER) -f Makefile.linux P2P=1 all

# ── unit tests ─────────────────────────────────────────────────────────────
$(OUTDIR)/unit_product_config: unit_product_config.c \
                                $(APP_SRC)/product_config.c \
                                $(APP_SRC)/product_config.h \
                                | $(OUTDIR)
	$(CC) $(CFLAGS) \
	    unit_product_config.c \
	    $(APP_SRC)/product_config.c \
	    $(DEPHY_CONFIG)/src/config_store.c \
	    -o $@

$(OUTDIR)/unit_bridge_control: unit_bridge_control.c \
                                $(APP_SRC)/bridge_control.c \
                                $(APP_SRC)/bridge_control.h \
                                $(APP_SRC)/product_config.c \
                                $(APP_SRC)/product_config.h \
                                | $(OUTDIR)
	$(CC) $(CFLAGS) \
	    unit_bridge_control.c \
	    $(APP_SRC)/bridge_control.c \
	    $(APP_SRC)/product_config.c \
	    $(DEPHY_CONFIG)/src/config_store.c \
	    -o $@

$(OUTDIR)/unit_product_runtime: unit_product_runtime.c \
                                $(APP_SRC)/product_runtime.c \
                                $(APP_SRC)/product_runtime.h \
                                $(APP_SRC)/product_config.c \
                                $(APP_SRC)/product_config.h \
                                | $(OUTDIR)
	$(CC) $(CFLAGS) \
	    unit_product_runtime.c \
	    $(APP_SRC)/product_runtime.c \
	    $(APP_SRC)/product_config.c \
	    $(DEPHY_CONFIG)/src/config_store.c \
	    -o $@

$(OUTDIR)/unit_product_topics: unit_product_topics.c \
                               $(APP_SRC)/product_topics.c \
                               $(APP_SRC)/product_topics.h \
                               $(APP_SRC)/product_config.h \
                               | $(OUTDIR)
	$(CC) $(CFLAGS) \
	    unit_product_topics.c \
	    $(APP_SRC)/product_topics.c \
	    -o $@

$(OUTDIR)/unit_product_console: unit_product_console.c \
                                $(APP_SRC)/product_console.c \
                                $(APP_SRC)/product_console.h \
                                $(APP_SRC)/product_config.c \
                                $(APP_SRC)/product_config.h \
                                $(APP_SRC)/product_runtime.c \
                                $(APP_SRC)/product_runtime.h \
                                $(DEPHY_CLI)/src/menu.c \
                                $(DEPHY_CLI)/include/dephy_cli/menu.h \
                                | $(OUTDIR)
	$(CC) $(CFLAGS) \
	    unit_product_console.c \
	    $(APP_SRC)/product_console.c \
	    $(DEPHY_CLI)/src/menu.c \
	    $(APP_SRC)/product_config.c \
	    $(DEPHY_CONFIG)/src/config_store.c \
	    $(APP_SRC)/product_runtime.c \
	    -o $@

$(OUTDIR)/unit_product_io_bridge: unit_product_io_bridge.c \
                                  $(APP_SRC)/product_io_bridge.c \
                                  $(APP_SRC)/product_io_bridge.h \
                                  $(DEPHY_IO)/src/payload.c \
                                  $(DEPHY_IO)/include/dephy_industrial_io/payload.h \
                                  $(DEPHY_IO)/include/dephy_industrial_io/industrial_io.h \
                                  | $(OUTDIR)
	$(CC) $(CFLAGS) \
	    unit_product_io_bridge.c \
	    $(APP_SRC)/product_io_bridge.c \
	    $(DEPHY_IO)/src/payload.c \
	    -o $@

$(OUTDIR)/unit_provisioning_http: unit_provisioning_http.c \
                                  $(APP_SRC)/provisioning_http.c \
                                  $(APP_SRC)/provisioning_http.h \
                                  $(APP_SRC)/product_config.c \
                                  $(APP_SRC)/product_config.h \
                                  $(APP_SRC)/product_runtime.c \
                                  $(APP_SRC)/product_runtime.h \
                                  $(APP_SRC)/product_topics.c \
                                  $(APP_SRC)/product_topics.h \
                                  | $(OUTDIR)
	$(CC) $(CFLAGS) \
	    unit_provisioning_http.c \
	    $(APP_SRC)/provisioning_http.c \
	    $(APP_SRC)/product_config.c \
	    $(DEPHY_CONFIG)/src/config_store.c \
	    $(APP_SRC)/product_runtime.c \
	    $(APP_SRC)/product_topics.c \
	    -pthread \
	    -o $@

unit-tests: $(OUTDIR)/unit_product_config \
            $(OUTDIR)/unit_product_runtime \
            $(OUTDIR)/unit_product_topics \
            $(OUTDIR)/unit_product_console \
            $(OUTDIR)/unit_product_io_bridge \
            $(OUTDIR)/unit_bridge_control \
            $(OUTDIR)/unit_provisioning_http
	@echo ""
	@echo "--- unit_product_config ---"
	$(OUTDIR)/unit_product_config
	@echo ""
	@echo "--- unit_product_runtime ---"
	$(OUTDIR)/unit_product_runtime
	@echo ""
	@echo "--- unit_product_topics ---"
	$(OUTDIR)/unit_product_topics
	@echo ""
	@echo "--- unit_product_console ---"
	$(OUTDIR)/unit_product_console
	@echo ""
	@echo "--- unit_product_io_bridge ---"
	$(OUTDIR)/unit_product_io_bridge
	@echo ""
	@echo "--- unit_bridge_control ---"
	$(OUTDIR)/unit_bridge_control
	@echo ""
	@echo "--- unit_provisioning_http ---"
	$(OUTDIR)/unit_provisioning_http

unit_product_config: $(OUTDIR)/unit_product_config
	$(OUTDIR)/unit_product_config

unit_bridge_control: $(OUTDIR)/unit_bridge_control
	$(OUTDIR)/unit_bridge_control

unit_product_runtime: $(OUTDIR)/unit_product_runtime
	$(OUTDIR)/unit_product_runtime

unit_product_topics: $(OUTDIR)/unit_product_topics
	$(OUTDIR)/unit_product_topics

unit_product_console: $(OUTDIR)/unit_product_console
	$(OUTDIR)/unit_product_console

unit_product_io_bridge: $(OUTDIR)/unit_product_io_bridge
	$(OUTDIR)/unit_product_io_bridge

unit_provisioning_http: $(OUTDIR)/unit_provisioning_http
	$(OUTDIR)/unit_provisioning_http

testkit-wrapper: testkit-unit-tests

testkit-unit-tests:
	@sh trigger_testkit.sh mqtt_field_bridge_unit \
	    $(MAKE) -C $(CURDIR) unit-tests

testkit-integration-tests:
	@sh trigger_testkit.sh mqtt_field_bridge_integration \
	    $(MAKE) -C $(CURDIR) integration-tests

testkit-stress:
	@sh trigger_testkit.sh mqtt_field_bridge_stress \
	    $(MAKE) -C $(CURDIR) stress

# ── integration tests ──────────────────────────────────────────────────────
integration-tests: broker-p2p
	@echo ""
	@echo "--- test_sync_deps ---"
	@sh test_sync_deps.sh
	@echo ""
	@echo "--- test_3node_scenario ---"
	@bash test_3node_scenario.sh

client-fallback-test:
	@bash test_fallback_mqtt_client.sh

hardware-esp32-chain-bridge-test: broker-p2p
	@bash test_esp32_linux_chain_bridge.sh

hardware-esp32-network-bind-test: broker-p2p
	@bash test_esp32_network_bind.sh

hardware-esp32-config-save-reboot-test: broker-p2p
	@bash test_esp32_config_save_reboot.sh

# ── scale tests ─────────────────────────────────────────────────────────────
scale-tests: broker-p2p
	@echo ""
	@echo "--- test_chain_scale (10 node chain) ---"
	@bash test_chain_scale.sh

scale-ring-tests: broker-p2p
	@echo ""
	@echo "--- test_chain_scale (10 node ring) ---"
	@TOPOLOGY=ring bash test_chain_scale.sh

# ── stress tests ───────────────────────────────────────────────────────────
stress: broker-p2p
	@echo ""
	@echo "--- stress_reconnect (5 cycles) ---"
	@bash stress_reconnect.sh
	@echo ""
	@echo "--- stress_throughput (5 pubs, 3 subs, 10s) ---"
	@bash stress_throughput.sh

# ── run everything ─────────────────────────────────────────────────────────
test: testkit-unit-tests testkit-integration-tests testkit-stress

clean:
	rm -rf $(OUTDIR)
