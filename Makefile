# Makefile — Container Orchestration System (Mini Docker)
# ML2011 Operating Systems — VIT Pune 2025-26
#
# FIRST TIME SETUP:
#   make setup    → Creates src/ and include/ structure from root files
#
# BUILD & RUN (after setup):
#   make role4    → Security & Deadlock demo  (no sudo needed)
#   sudo make role1     → Architecture / namespaces demo
#   sudo make role2     → Memory & storage demo
#   sudo make role3     → CPU scheduler demo
#   sudo make role5     → Monitoring dashboard demo
#   make clean    → Remove all binaries

CC      = gcc
CFLAGS  = -Wall -Wextra -g -I./include
LDFLAGS = -lpthread

SRC_DIR = src
BIN_DIR = bin

# Auto-create bin dir before any build target
$(shell mkdir -p $(BIN_DIR))

# ── First-time directory structure setup ────────────────────
setup:
	mkdir -p src include
	@for f in role1_architecture.c role2_memory_storage.c \
	           role3_scheduler.c role4_security_sync.c \
	           role5_monitoring.c; do \
	  if [ ! -f src/$$f ]; then \
	    cp $$f src/$$f; \
	    echo "  copied $$f -> src/$$f"; \
	  fi; \
	done
	@if [ ! -f include/container.h ]; then \
	  cp container.h include/container.h; \
	  echo "  copied container.h -> include/container.h"; \
	fi
	@echo ""
	@echo "Setup complete! Now run your role:"
	@echo "  make role4              # no sudo needed"
	@echo "  sudo make role1         # needs root"

# ── Individual role demos ───────────────────────────────────
role1:
	$(CC) $(CFLAGS) -DROLE1_DEMO \
		$(SRC_DIR)/role1_architecture.c \
		-o $(BIN_DIR)/role1_demo $(LDFLAGS)
	@echo "Built. Run: sudo ./$(BIN_DIR)/role1_demo"

role2:
	$(CC) $(CFLAGS) -DROLE2_DEMO \
		$(SRC_DIR)/role2_memory_storage.c \
		-o $(BIN_DIR)/role2_demo $(LDFLAGS)
	@echo "Built. Run: sudo ./$(BIN_DIR)/role2_demo"

role3:
	$(CC) $(CFLAGS) -DROLE3_DEMO \
		$(SRC_DIR)/role3_scheduler.c \
		-o $(BIN_DIR)/role3_demo $(LDFLAGS)
	@echo "Built. Run: sudo ./$(BIN_DIR)/role3_demo"

role4:
	$(CC) $(CFLAGS) -DROLE4_DEMO \
		$(SRC_DIR)/role4_security_sync.c \
		-o $(BIN_DIR)/role4_demo $(LDFLAGS)
	@echo "Built. Run: ./$(BIN_DIR)/role4_demo"

role5:
	$(CC) $(CFLAGS) -DROLE5_DEMO \
		$(SRC_DIR)/role5_monitoring.c \
		$(SRC_DIR)/role1_architecture.c \
		-o $(BIN_DIR)/role5_demo $(LDFLAGS)
	@echo "Built. Run: sudo ./$(BIN_DIR)/role5_demo"

clean:
	rm -rf $(BIN_DIR)

.PHONY: setup role1 role2 role3 role4 role5 clean
