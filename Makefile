CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 $(shell pkg-config --cflags sdl3 sdl3-image sdl3-ttf libexif)
LDFLAGS = $(shell pkg-config --libs sdl3 sdl3-image sdl3-ttf libexif) -lm -lpthread

SRCS = src/main.c src/utils.c src/app.c src/fileops.c src/loader.c src/cache.c src/viewer.c src/input.c src/overlay.c src/anim.c src/exif.c
OBJS = $(SRCS:.c=.o)
TARGET = frame

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
