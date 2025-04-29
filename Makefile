CC = g++
CFLAGS = -std=c++17 -g -Wall
SRC_DIR = src
OBJ_DIR = obj
SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))
TARGET = L1simulate

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CC) $(CFLAGS) -I include -c $< -o $@

$(OBJ_DIR):
	mkdir -p $@

clean:
	rm -f $(TARGET) $(OBJS)
	rm -rf $(OBJ_DIR)

.PHONY: all clean