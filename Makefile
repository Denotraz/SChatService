# --- Base config ---

SRC    = gui.c
TARGET = client

CC     = gcc
CFLAGS = -Wall -g `pkg-config --cflags gtk+-3.0`
LIBS   = `pkg-config --libs gtk+-3.0`

UNAME_S := $(shell uname -s)

# Windows (MSYS2 / MinGW)
ifeq ($(findstring MINGW,$(UNAME_S)),MINGW)
    CC     = x86_64-w64-mingw32-gcc
    TARGET = chat_client.exe
    LIBS  += -lws2_32
endif

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

clean:
	rm -f $(TARGET)
