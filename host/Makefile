CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CORE_DIR := ../src/NoiseMachine
INCLUDES := -I$(CORE_DIR)
OUT_DIR := ../out

all: noise_host

noise_host: main.c $(CORE_DIR)/noise_core.c $(CORE_DIR)/noise_core.h
	$(CC) $(CFLAGS) $(INCLUDES) main.c $(CORE_DIR)/noise_core.c -lm -o $@

run: noise_host
	@mkdir -p $(OUT_DIR)
	./noise_host -k pink -d 10 $(OUT_DIR)/pink.wav
	afplay $(OUT_DIR)/pink.wav

clean:
	rm -f noise_host

.PHONY: all run clean
