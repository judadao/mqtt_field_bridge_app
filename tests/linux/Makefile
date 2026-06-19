ROOT    := ../..
BROKER  := $(ROOT)/deps/mqtt_min_broker
APP_SRC := $(ROOT)/app/src

CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L \
         -I$(APP_SRC) \
         -Iinclude \
         -I$(BROKER)/include
OUTDIR = out

.PHONY: all test unit-tests unit_product_config unit_product_runtime \
        unit_product_topics unit_bridge_control \
        unit_provisioning_http integration-tests stress scale-tests \
        scale-ring-tests clean broker-p2p

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

$(OUTDIR)/unit_provisioning_http: unit_provisioning_http.c \
                                   $(APP_SRC)/provisioning_http.c \
                                   $(APP_SRC)/provisioning_http.h \
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

unit-tests: $(OUTDIR)/unit_product_config \
            $(OUTDIR)/unit_product_runtime \
            $(OUTDIR)/unit_product_topics \
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

unit_provisioning_http: $(OUTDIR)/unit_provisioning_http
	$(OUTDIR)/unit_provisioning_http

# ── integration tests ──────────────────────────────────────────────────────
integration-tests: broker-p2p
	@echo ""
	@echo "--- test_sync_deps ---"
	@sh test_sync_deps.sh
	@echo ""
	@echo "--- test_3node_scenario ---"
	@bash test_3node_scenario.sh

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
test: unit-tests integration-tests stress

clean:
	rm -rf $(OUTDIR)
