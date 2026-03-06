# Makefile for Canvas Project

CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -g -D_GNU_SOURCE -fstack-protector-strong
INCLUDES = -Iinclude -Ideps/cJSON -Itests/unity

# Libraries
LIBS_GUI = $(shell pkg-config --libs gtk+-3.0 cairo)
LIBS_DB = $(shell mysql_config --libs)
# LIBS_NET = -lwebsockets
LIBS_NET = 
LIBS_M = -lm
LIBS = $(LIBS_GUI) $(LIBS_DB) $(LIBS_NET) $(LIBS_M)

CFLAGS += $(shell pkg-config --cflags gtk+-3.0 cairo)
CFLAGS += $(shell mysql_config --cflags)

# Directories
SRC_DIR = src
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin
DEPS_DIR = deps
TEST_DIR = tests

# Sources
SRCS_COMMON = $(wildcard $(SRC_DIR)/common/*.c) \
              $(wildcard $(SRC_DIR)/protocol/*.c) \
              $(wildcard $(SRC_DIR)/utils/*.c) \
              $(DEPS_DIR)/cJSON/cJSON.c

SRCS_SERVER = $(wildcard $(SRC_DIR)/server/*.c) $(SRCS_COMMON)
SRCS_CLIENT = $(wildcard $(SRC_DIR)/client/*.c) $(SRCS_COMMON)

SRCS_TEST = $(TEST_DIR)/test_protocol.c \
            $(TEST_DIR)/unity/unity.c \
            $(SRC_DIR)/protocol/json_codec.c \
            $(DEPS_DIR)/cJSON/cJSON.c

# Objects
OBJS_SERVER = $(patsubst %.c, $(OBJ_DIR)/%.o, $(notdir $(SRCS_SERVER)))
OBJS_CLIENT = $(patsubst %.c, $(OBJ_DIR)/%.o, $(notdir $(SRCS_CLIENT)))

# Targets
TARGET_SERVER = $(BIN_DIR)/canvas_server
TARGET_CLIENT = $(BIN_DIR)/canvas_client
TARGET_TEST = $(BIN_DIR)/test_protocol

# VPATH for finding source files
VPATH = $(SRC_DIR)/server $(SRC_DIR)/client $(SRC_DIR)/common $(SRC_DIR)/protocol $(SRC_DIR)/utils $(DEPS_DIR)/cJSON $(TEST_DIR) $(TEST_DIR)/unity

.PHONY: all clean directories server client test_protocol

all: directories server client

server: $(TARGET_SERVER)
client: $(TARGET_CLIENT)

directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)

$(TARGET_SERVER): $(OBJS_SERVER)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

$(TARGET_CLIENT): $(OBJS_CLIENT)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $^ $(LIBS)

# Compile rule
$(OBJ_DIR)/%.o: %.c | directories
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Test target
test_protocol: directories
	$(CC) $(CFLAGS) $(INCLUDES) -o $(TARGET_TEST) $(SRCS_TEST) $(LIBS_M)
	./$(TARGET_TEST)

clean:
	rm -rf $(BUILD_DIR)

# Test (Placeholder)
test: test_protocol
