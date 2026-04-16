CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11 -O2
DEBUG_FLAGS = -g -DDEBUG

TARGET = mercury
BUILD_DIR = build
BIN = $(BUILD_DIR)/$(TARGET)

SOURCES = src/main.cpp
OBJECTS = $(BUILD_DIR)/main.o

SERVICE_FILE_SRC = systemd/$(TARGET).service
SERVICE_FILE_DEST = /etc/systemd/system/$(TARGET).service

all: $(BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN): $(BUILD_DIR) $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS)
	@echo "Build complete: $@"

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

debug: CXXFLAGS += $(DEBUG_FLAGS)
debug: clean $(BIN)
	@echo "Debug build complete: $(BIN)"

clean:
	rm -rf $(BUILD_DIR)
	@echo "Clean complete"

install: $(BIN)
	sudo cp $(BIN) /usr/local/bin/
	@echo "Installed $(TARGET) to /usr/local/bin/"

uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)
	sudo systemctl stop $(TARGET).service || true
	sudo systemctl disable $(TARGET).service || true
	sudo rm -f $(SERVICE_FILE_DEST)
	sudo systemctl daemon-reload
	@echo "Uninstalled $(TARGET) and removed systemd service"

run: $(BIN)
	./$(BIN)

test: $(BIN)
	@echo "Starting server in background..."
	@./$(BIN) &
	@sleep 2
	@echo "Testing POST request..."
	@curl -X POST -d "Test message from makefile" http://localhost:9999
	@echo ""
	@echo "Testing GET request..."
	@curl http://localhost:9999
	@echo ""
	@echo "Stopping server..."
	@pkill -f "$(BIN)" || true
	@echo "Test complete"

setup-service: install
	sudo bash scripts/setup.sh

help:
	@echo "Available targets:"
	@echo "  all            - Build the server (default)"
	@echo "  debug          - Clean and build with debug symbols"
	@echo "  clean          - Remove build artifacts"
	@echo "  install        - Install to /usr/local/bin (requires sudo)"
	@echo "  uninstall      - Remove binary and disable systemd service"
	@echo "  setup-service  - Install and enable systemd service"
	@echo "  run            - Build and run the server"
	@echo "  test           - Build and run automated tests"
	@echo "  help           - Show this help message"

.PHONY: all debug clean install uninstall run test setup-service help
