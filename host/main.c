#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "noise_core.h"

#define WAVE_HEADER_SIZE 44u
#define HOST_BATCH 256u

static void write_u32_le(FILE *file, uint32_t value) {
  fputc((int)(value & 0xffu), file);
  fputc((int)((value >> 8) & 0xffu), file);
  fputc((int)((value >> 16) & 0xffu), file);
  fputc((int)((value >> 24) & 0xffu), file);
}

static void write_u16_le(FILE *file, uint16_t value) {
  fputc((int)(value & 0xffu), file);
  fputc((int)((value >> 8) & 0xffu), file);
}

static void write_tag(FILE *file, const char tag[4]) {
  fwrite(tag, 1, 4, file);
}

static int write_wav_header(FILE *file, uint32_t frames) {
  uint32_t data_size = frames * sizeof(int16_t);
  write_tag(file, "RIFF");
  write_u32_le(file, WAVE_HEADER_SIZE - 8 + data_size);
  write_tag(file, "WAVE");
  write_tag(file, "fmt ");
  write_u32_le(file, 16);
  write_u16_le(file, 1);
  write_u16_le(file, 1);
  write_u32_le(file, NOISE_SAMPLE_RATE_HZ);
  write_u32_le(file, NOISE_SAMPLE_RATE_HZ * sizeof(int16_t));
  write_u16_le(file, sizeof(int16_t));
  write_u16_le(file, 16);
  write_tag(file, "data");
  write_u32_le(file, data_size);
  return ferror(file);
}

static void print_usage(const char *program) {
  fprintf(stderr,
          "usage: %s [-k white|pink] [-s seed] [-d seconds] OUTPUT.wav\n",
          program);
}

int main(int argc, char **argv) {
  noise_kind kind = NOISE_KIND_PINK;
  uint32_t seed = 1;
  double seconds = 3.0;
  const char *out_path = NULL;

  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    char *end = NULL;
    if (strcmp(arg, "-k") == 0 && i + 1 < argc) {
      const char *kind_arg = argv[++i];
      if (strcmp(kind_arg, "white") == 0) {
        kind = NOISE_KIND_WHITE;
      } else if (strcmp(kind_arg, "pink") == 0) {
        kind = NOISE_KIND_PINK;
      } else {
        fprintf(stderr, "unknown kind: %s\n", kind_arg);
        return 1;
      }
    } else if (strcmp(arg, "-s") == 0 && i + 1 < argc) {
      unsigned long parsed = strtoul(argv[++i], &end, 10);
      if (*end || parsed > UINT32_MAX) {
        fprintf(stderr, "bad seed\n");
        return 1;
      }
      seed = (uint32_t)parsed;
    } else if (strcmp(arg, "-d") == 0 && i + 1 < argc) {
      seconds = strtod(argv[++i], &end);
      if (*end || seconds <= 0.0) {
        fprintf(stderr, "bad duration\n");
        return 1;
      }
    } else if (out_path) {
      print_usage(argv[0]);
      return 1;
    } else {
      out_path = arg;
    }
  }

  if (!out_path) {
    print_usage(argv[0]);
    return 1;
  }

  uint64_t frames64 = (uint64_t)(seconds * NOISE_SAMPLE_RATE_HZ);
  if (frames64 == 0 || frames64 > UINT32_MAX / sizeof(int16_t)) {
    fprintf(stderr, "duration outside WAV range\n");
    return 1;
  }
  uint32_t frames = (uint32_t)frames64;

  static noise_gen gen;
  int16_t *batch = malloc(HOST_BATCH * sizeof(int16_t));
  if (!batch) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }
  noise_init(&gen, kind, seed);

  FILE *file = fopen(out_path, "wb");
  if (!file) {
    fprintf(stderr, "cannot open %s\n", out_path);
    return 1;
  }
  if (write_wav_header(file, frames)) {
    fprintf(stderr, "cannot write header to %s\n", out_path);
    fclose(file);
    return 1;
  }

  uint32_t remaining = frames;
  while (remaining) {
    size_t count = remaining < HOST_BATCH ? remaining : HOST_BATCH;
    noise_fill(&gen, batch, count);
    if (fwrite(batch, sizeof(int16_t), count, file) != count) {
      fprintf(stderr, "cannot write %s\n", out_path);
      fclose(file);
      return 1;
    }
    remaining -= (uint32_t)count;
  }

  if (fclose(file)) {
    fprintf(stderr, "cannot close %s\n", out_path);
    return 1;
  }
  printf("%s: %u frames of %s noise, seed %lu\n", out_path, frames,
         kind == NOISE_KIND_WHITE ? "white" : "pink", (unsigned long)seed);
  return 0;
}
