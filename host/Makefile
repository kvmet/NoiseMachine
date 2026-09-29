CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CORE_DIR := ../src/NoiseMachine
INCLUDES := -I$(CORE_DIR)
CORE_SOURCES := $(wildcard $(CORE_DIR)/*.c)
CORE_HEADERS := $(wildcard $(CORE_DIR)/*.h)
OUT_DIR := ../out

all: noise_host

noise_host: main.c $(CORE_SOURCES) $(CORE_HEADERS)
	$(CC) $(CFLAGS) $(INCLUDES) main.c $(CORE_SOURCES) -lm -o $@

gui: noise_gui

noise_gui: gui.m $(CORE_SOURCES) $(CORE_HEADERS)
	$(CC) $(CFLAGS) -fobjc-arc $(INCLUDES) gui.m $(CORE_SOURCES) \
		-lm -framework Cocoa -framework AudioToolbox -o $@

run: noise_host
	@mkdir -p $(OUT_DIR)
	./noise_host -k pink -d 10 $(OUT_DIR)/pink.wav
	afplay $(OUT_DIR)/pink.wav

clean:
	rm -f noise_host noise_gui

.PHONY: all gui run clean
