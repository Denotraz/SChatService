# Compiler
CC = gcc

# Source files
SRC = gui.c

# Output binary
TARGET = client

# GTK compile and link flags via pkg-config
GTK_FLAGS = $(shell pkg-config --cflags --libs gtk+-3.0)

# Default rule
all: $(TARGET)

# Build the executable
$(TARGET): $(SRC)
	$(CC) $(SRC) -o $(TARGET) $(GTK_FLAGS)

# Remove compiled files
clean:
	rm -f $(TARGET)
