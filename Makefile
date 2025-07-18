CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11 -O2
DEBUG_FLAGS = -g -DDEBUG

TARGET = mercury

SOURCES = src/main.cpp

BUILD_DIR = build

OBJECTS = $(BUILD_DIR)/main.o

SERVICE_FILE_SRC = systemd/$(TARGET).service
SERVICE_FILE_DEST = /etc/systemd/system/$(TARGET).service

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $(BUILD_DIR)/$(TARGET) $(OBJECTS)
	@echo "Build complete: $(BUILD_DIR)/$(TARGET)"

$(BUILD_DIR)/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

debug: CXXFLAGS += $(DEBUG_FLAGS)
debug: $(TARGET)
	@echo "Debug build complete: $(BUILD_DIR)/$(TARGET)"

clean:
	rm -rf $(BUILD_DIR)
	@echo "Clean complete"

install: $(TARGET)
	sudo cp $(BUILD_DIR)/$(TARGET) /usr/local/bin/
	@echo "Installed $(TARGET) to /usr/local/bin/"

uninstall:
	sudo rm -f /usr/local/bin/$(TARGET)
	sudo systemctl stop $(TARGET).service || true
	sudo systemctl disable $(TARGET).service || true
	sudo rm -f $(SERVICE_FILE_DEST)
	sudo systemctl daemon-reload
	@echo "Uninstalled $(TARGET) and removed systemd service"

run: $(TARGET)
	./$(BUILD_DIR)/$(TARGET)

test: $(TARGET)
	@echo "Starting server in background..."
	@./$(BUILD_DIR)/$(TARGET) &
	@sleep 2
	@echo "Testing POST request..."
	@curl -X POST -d "Test message from makefile" http://localhost:9999
	@echo ""
	@echo "Testing GET request..."
	@curl http://localhost:9999
	@echo ""
	@echo "Stopping server..."
	@pkill -f "./$(BUILD_DIR)/$(TARGET)"
	@echo "Test complete"

setup-service: install
	sudo bash scripts/setup.sh

help:
	@echo "Available targets:"
	@echo "  all            - Build the server (default)"
	@echo "  debug          - Build with debug symbols"
	@echo "  clean          - Remove build artifacts"
	@echo "  install        - Install to /usr/local/bin (requires sudo)"
	@echo "  uninstall      - Remove binary and disable systemd service"
	@echo "  setup-service  - Install and enable systemd service (requires sudo)"
	@echo "  run            - Build and run the server"
	@echo "  test           - Build and run automated tests"
	@echo "  help           - Show this help message"

.PHONY: all debug clean install uninstall run test setup-service help
