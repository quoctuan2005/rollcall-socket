.PHONY: build run stop clean help

# Paths
SRC_DIR = src
BUILD_DIR = build
SERVER_BIN = $(BUILD_DIR)/server

# Compiler
CXX = clang++
CXXFLAGS = -std=c++17 -O2 -Wall

# Default target
help:
	@echo "📚 Makefile commands:"
	@echo "  make build    - Biên dịch server"
	@echo "  make run      - Chạy server"
	@echo "  make stop     - Dừng server"
	@echo "  make clean    - Xoá binary"
	@echo "  make help     - Hiển thị trợ giúp"

# Build target
build:
	@echo "🔨 Compiling server..."
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SRC_DIR)/main.cpp -o $(SERVER_BIN)
	@echo "✅ Done! Binary: $(SERVER_BIN)"

# Run target
run: build
	@echo "🚀 Starting server on port 8080..."
	@echo "👤 Student: http://localhost:8080"
	@echo "👨‍🏫 Admin: http://localhost:8080/admin"
	@./$(SERVER_BIN) &

# Stop target
stop:
	@echo "🛑 Stopping server..."
	@lsof -i :8080 | grep server | grep -v COMMAND | awk '{print $$2}' | xargs kill -9 2>/dev/null || true
	@echo "✅ Server stopped"

# Clean target
clean:
	@echo "🧹 Cleaning..."
	@rm -f $(SERVER_BIN)
	@echo "✅ Clean done"

# Quick rebuild & run
fresh: clean build run
	@echo "✨ Fresh start!"
