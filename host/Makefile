CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CORE_DIR := ../src/NoiseMachine
INCLUDES := -I$(CORE_DIR)
OUT_DIR := ../out

all: noise_host

noise_host: main.c $(CORE_DIR)/noise_core.c $(CORE_DIR)/noise_core.h
	$(CC) $(CFLAGS) $(INCLUDES) main.c $(CORE_DIR)/noise_core.c -lm -o $@

gui: noise_gui

noise_gui: gui.m $(CORE_DIR)/noise_core.c $(CORE_DIR)/noise_core.h
	$(CC) $(CFLAGS) -fobjc-arc $(INCLUDES) gui.m $(CORE_DIR)/noise_core.c \
		-lm -framework Cocoa -framework AudioToolbox -o $@

run: noise_host
	@mkdir -p $(OUT_DIR)
	./noise_host -k pink -d 10 $(OUT_DIR)/pink.wav
	afplay $(OUT_DIR)/pink.wav

clean:
	rm -f noise_host noise_gui

.PHONY: all gui run clean
