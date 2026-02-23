CC = gcc
TARGET = velvet
SRC_DIR = src
OBJ_DIR = obj
INC_DIR = include

ifeq ($(OS),Windows_NT)
    PLATFORM_OS = WINDOWS
else
    PLATFORM_OS = LINUX
    PKG_LIBS = libpipewire-0.3 libspa-0.2 libzmq
    CFLAGS = -Wall -Wextra -g $(shell pkg-config --cflags $(PKG_LIBS)) -I$(INC_DIR) -I$(SRC_DIR)
    LDFLAGS = $(shell pkg-config --libs $(PKG_LIBS)) -lraylib -lm -lpthread -ldl -lrt -lX11
endif

SRCS = $(wildcard $(SRC_DIR)/*.c) $(wildcard $(SRC_DIR)/components/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
