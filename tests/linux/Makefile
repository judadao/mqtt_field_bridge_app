ROOT    := ../..
BROKER  := $(ROOT)/deps/mqtt_min_broker
APP_SRC := $(ROOT)/app/src

CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -D_POSIX_C_SOURCE=200809L \
         -I$(APP_SRC) \
         -Iinclude \
         -I$(BROKER)/include
OUTDIR = out

.PHONY: all test unit-tests integration-tests stress clean broker-p2p

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

unit-tests: $(OUTDIR)/unit_product_config
	@echo ""
	@echo "--- unit_product_config ---"
	$(OUTDIR)/unit_product_config

# ── integration tests ──────────────────────────────────────────────────────
integration-tests: broker-p2p
	@echo ""
	@echo "--- test_sync_deps ---"
	@sh test_sync_deps.sh
	@echo ""
	@echo "--- test_3node_scenario ---"
	@sh test_3node_scenario.sh

# ── stress tests ───────────────────────────────────────────────────────────
stress: broker-p2p
	@echo ""
	@echo "--- stress_reconnect (10 cycles) ---"
	@sh stress_reconnect.sh
	@echo ""
	@echo "--- stress_throughput (5 pubs, 3 subs, 10s) ---"
	@sh stress_throughput.sh

# ── run everything ─────────────────────────────────────────────────────────
test: unit-tests integration-tests stress

clean:
	rm -rf $(OUTDIR)
