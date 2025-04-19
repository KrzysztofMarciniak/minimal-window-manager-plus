AUDIO_SCRIPT ?= "$(shell pwd)/audio.sh"
CC = gcc
CFLAGS = -Wall -Wextra -pedantic -O3 -march=native -flto -ffast-math -fomit-frame-pointer -ffunction-sections -fdata-sections -DAUDIO_SCRIPT="\"$(AUDIO_SCRIPT)\""
LDFLAGS = -lX11 -Wl,--gc-sections -Wl,--as-needed -Wl,-O1 -lm
TARGET = mwm
SRC = main.c
PREFIX = /usr/local

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

install: $(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	strip --strip-all $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
