# Makefile — Container Orchestration System
# Build individual role demos or the full system
#
# Usage:
#   make role1    → Build & run Architecture Lead demo
#   make role2    → Build & run Memory & Storage demo
#   make role3    → Build & run Scheduler demo
#   make role4    → Build & run Security & Sync demo
#   make role5    → Build & run Monitoring demo
#   make all      → Build complete system
#   make clean    → Remove all binaries

CC      = gcc
CFLAGS  = -Wall -Wextra -g -I./include
LDFLAGS = -lpthread

SRC_DIR = src
BIN_DIR = bin

# Create bin dir
$(shell mkdir -p $(BIN_DIR))

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

# ── Full system ─────────────────────────────────────────────
all:
	$(CC) $(CFLAGS) \
		$(SRC_DIR)/role1_architecture.c \
		$(SRC_DIR)/role2_memory_storage.c \
		$(SRC_DIR)/role3_scheduler.c \
		$(SRC_DIR)/role4_security_sync.c \
		$(SRC_DIR)/role5_monitoring.c \
		-o $(BIN_DIR)/container_system $(LDFLAGS)
	@echo "Full system built: ./$(BIN_DIR)/container_system"

clean:
	rm -rf $(BIN_DIR)

.PHONY: role1 role2 role3 role4 role5 all clean
