ROOT    := ../..
BROKER  := $(ROOT)/deps/mqtt_min_broker
DEPHY_IO := $(ROOT)/deps/dephy_industrial_io
APP_SRC := $(ROOT)/app/src
WEB_DIR := $(ROOT)/app/web
WEB_SRC := $(WEB_DIR)/index.html $(WEB_DIR)/styles.css $(WEB_DIR)/app.js
WEB_HDR := $(APP_SRC)/generated/provisioning_index.h
WEB_EMBED := $(ROOT)/scripts/build_provisioning_web.js

CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L \
         -I$(APP_SRC) \
         -Iinclude \
         -I$(BROKER)/include \
         -I$(DEPHY_IO)/include
OUTDIR = out

.PHONY: all test unit-tests unit_product_config unit_product_runtime \
        unit_product_topics unit_bridge_control \
        unit_product_console \
        unit_product_io_bridge \
        unit_provisioning_http web-network-test ui-browser-test run-web-server run_web_config_server integration-tests hardware-esp32-chain-bridge-test stress scale-tests \
        scale-ring-tests clean broker-p2p

all: unit-tests

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(WEB_HDR): $(WEB_SRC) $(WEB_EMBED)
	node $(WEB_EMBED) $(WEB_DIR) $(WEB_HDR) index_html

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
                                | $(OUTDIR)
	$(CC) $(CFLAGS) \
	    unit_product_console.c \
	    $(APP_SRC)/product_console.c \
	    $(APP_SRC)/product_config.c \
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
                                   $(WEB_HDR) \
                                   $(APP_SRC)/product_config.c \
                                   $(APP_SRC)/product_runtime.c \
                                   $(APP_SRC)/product_topics.c \
                                   $(APP_SRC)/bridge_control.c \
                                   | $(OUTDIR)
	$(CC) $(CFLAGS) \
	    unit_provisioning_http.c \
	    $(APP_SRC)/provisioning_http.c \
	    $(APP_SRC)/product_config.c \
	    $(APP_SRC)/product_runtime.c \
	    $(APP_SRC)/product_topics.c \
	    $(APP_SRC)/bridge_control.c \
	    -o $@ -lpthread

$(OUTDIR)/run_web_config_server: run_web_config_server.c \
                                  $(APP_SRC)/provisioning_http.c \
                                  $(APP_SRC)/provisioning_http.h \
                                  $(WEB_HDR) \
                                  $(APP_SRC)/product_config.c \
                                  $(APP_SRC)/product_runtime.c \
                                  $(APP_SRC)/product_topics.c \
                                  $(APP_SRC)/bridge_control.c \
                                  | $(OUTDIR)
	$(CC) $(CFLAGS) \
	    run_web_config_server.c \
	    $(APP_SRC)/provisioning_http.c \
	    $(APP_SRC)/product_config.c \
	    $(APP_SRC)/product_runtime.c \
	    $(APP_SRC)/product_topics.c \
	    $(APP_SRC)/bridge_control.c \
	    -o $@ -lpthread

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

web-network-test: $(OUTDIR)/unit_provisioning_http
	@bash test_web_network_config.sh

provisioning-render-size: $(WEB_HDR)
	@sh test_provisioning_render_size.sh

testkit-wrapper:
	@sh test_with_testkit.sh

ui-browser-test: $(OUTDIR)/run_web_config_server
	@node ui_browser_test.js

run-web-server: $(OUTDIR)/run_web_config_server
	@bash run_web_config_server.sh

run_web_config_server: run-web-server

# ── integration tests ──────────────────────────────────────────────────────
integration-tests: broker-p2p
	@echo ""
	@echo "--- test_sync_deps ---"
	@sh test_sync_deps.sh
	@echo ""
	@echo "--- test_3node_scenario ---"
	@bash test_3node_scenario.sh

hardware-esp32-chain-bridge-test: broker-p2p
	@bash test_esp32_linux_chain_bridge.sh

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
test: unit-tests provisioning-render-size testkit-wrapper integration-tests stress

clean:
	rm -rf $(OUTDIR)
