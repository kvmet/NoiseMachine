#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "noise_core.h"

#define HOST_BATCH 256u
#define WAV_FRAME_BYTES (NOISE_CHANNELS * 2u)

static void write_u32_le(FILE *file, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) fputc((int)((value >> (8 * i)) & 0xffu), file);
}

static void write_u16_le(FILE *file, uint16_t value) {
  fputc((int)(value & 0xffu), file);
  fputc((int)(value >> 8), file);
}

static int write_wav_header(FILE *file, uint32_t frames) {
  uint32_t data_size = frames * WAV_FRAME_BYTES;
  fwrite("RIFF", 1, 4, file);
  write_u32_le(file, 36u + data_size);
  fwrite("WAVEfmt ", 1, 8, file);
  write_u32_le(file, 16);
  write_u16_le(file, 1);
  write_u16_le(file, NOISE_CHANNELS);
  write_u32_le(file, NOISE_SAMPLE_RATE_HZ);
  write_u32_le(file, NOISE_SAMPLE_RATE_HZ * WAV_FRAME_BYTES);
  write_u16_le(file, WAV_FRAME_BYTES);
  write_u16_le(file, 16);
  fwrite("data", 1, 4, file);
  write_u32_le(file, data_size);
  return !ferror(file);
}

static void print_usage(const char *program) {
  fprintf(stderr,
      "usage: %s [options] OUTPUT.wav\n"
      "  -k white|pink|hum50|hum60|wind|crickets|cicadas|rain   repeat to mix layers\n"
      "  -r intensity   fixed rain intensity, 0..1\n"
      "  -v             vary rain with the Markov controller\n"
      "  -l minimum     minimum varying intensity, 0..1\n"
      "  -u maximum     maximum varying intensity, 0..1\n"
      "  -m mixed|water|dirt|leaf|concrete|glass|metal|plastic|asphalt|asphalt-roof\n"
      "  -n rate        arrivals/second at full intensity, 0..2000\n"
      "  -b metres      ear spacing / head diameter, 0..0.5; default 0.18\n"
      "  -a amount      head model strength, 0..1; 0 bypasses it\n"
      "  -f amount      rear filter strength, 0..1; 0 bypasses it\n"
      "  -e gain        reverb gain, 0..1\n"
      "  -g gain        master gain, 0..1\n"
      "  -s seed        unsigned 32-bit integer\n"
      "  -d seconds     duration, default 10\n", program);
}

static int parse_number(const char *text, double *value) {
  char *end;
  errno = 0;
  *value = strtod(text, &end);
  return text[0] && end != text && !*end && errno != ERANGE && isfinite(*value);
}

int main(int argc, char **argv) {
  noise_config config;
  noise_config_default(&config);
  memset(config.ambient_gain, 0, sizeof(config.ambient_gain));
  uint32_t seed = 1;
  double seconds = 10.0;
  const char *out_path = NULL;
  int explicit_layer = 0;
  int explicit_intensity = 0;
  int rain = 0;

  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (strcmp(arg, "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    if (strcmp(arg, "-v") == 0) {
      config.vary_rain = 1;
      rain = 1;
      continue;
    }
    if (arg[0] != '-') {
      if (out_path) {
        print_usage(argv[0]);
        return 1;
      }
      out_path = arg;
      continue;
    }
    if (i + 1 == argc) {
      fprintf(stderr, "missing value for %s\n", arg);
      return 1;
    }
    const char *value = argv[++i];
    if (strcmp(arg, "-k") == 0) {
      explicit_layer = 1;
      if (strcmp(value, "rain") == 0) {
        rain = 1;
      } else {
        static const char *names[] = {
          "white", "pink", "hum50", "hum60", "wind", "crickets", "cicadas"
        };
        unsigned kind = 0;
        while (kind < NOISE_KIND_COUNT && strcmp(value, names[kind])) ++kind;
        if (kind == NOISE_KIND_COUNT) {
          fprintf(stderr, "unknown layer: %s\n", value);
          return 1;
        }
        config.ambient_gain[kind] = 0.3f;
      }
    } else if (strcmp(arg, "-m") == 0) {
      static const char *names[] = {
        "water", "dirt", "leaf", "concrete", "glass", "metal", "plastic", "asphalt",
        "asphalt-roof"
      };
      if (strcmp(value, "mixed") == 0) {
        noise_config defaults;
        noise_config_default(&defaults);
        memcpy(config.surface_weight, defaults.surface_weight, sizeof(config.surface_weight));
      } else {
        unsigned surface = 0;
        while (surface < NOISE_SURFACE_COUNT && strcmp(value, names[surface])) ++surface;
        if (surface == NOISE_SURFACE_COUNT) {
          fprintf(stderr, "unknown surface: %s\n", value);
          return 1;
        }
        memset(config.surface_weight, 0, sizeof(config.surface_weight));
        config.surface_weight[surface] = 1.0f;
      }
    } else if (strcmp(arg, "-s") == 0) {
      char *end;
      errno = 0;
      unsigned long parsed = strtoul(value, &end, 10);
      if (value[0] < '0' || value[0] > '9' || *end || errno == ERANGE || parsed > UINT32_MAX) {
        fprintf(stderr, "invalid seed: %s\n", value);
        return 1;
      }
      seed = (uint32_t)parsed;
    } else {
      double number;
      if (!parse_number(value, &number)) {
        fprintf(stderr, "invalid value for %s: %s\n", arg, value);
        return 1;
      }
      if (strcmp(arg, "-d") == 0) {
        seconds = number;
        continue;
      }
      double maximum = strcmp(arg, "-n") == 0 ? 2000.0 :
          (strcmp(arg, "-b") == 0 ? 0.5 : 1.0);
      if (number < 0.0 || number > maximum) {
        fprintf(stderr, "value for %s outside 0..%g\n", arg, maximum);
        return 1;
      }
      if (strcmp(arg, "-r") == 0) {
        config.rain_intensity = (float)number;
        explicit_intensity = 1;
        rain = 1;
      } else if (strcmp(arg, "-l") == 0) {
        config.min_rain_intensity = (float)number;
      } else if (strcmp(arg, "-u") == 0) {
        config.max_rain_intensity = (float)number;
      } else if (strcmp(arg, "-n") == 0) {
        config.max_drops_per_s = (float)number;
      } else if (strcmp(arg, "-e") == 0) {
        config.reverb_gain = (float)number;
      } else if (strcmp(arg, "-b") == 0) {
        config.stereo_width_m = (float)number;
      } else if (strcmp(arg, "-a") == 0) {
        config.head_amount = (float)number;
      } else if (strcmp(arg, "-f") == 0) {
        config.rear_amount = (float)number;
      } else if (strcmp(arg, "-g") == 0) {
        config.master_gain = (float)number;
      } else {
        fprintf(stderr, "unknown option: %s\n", arg);
        return 1;
      }
    }
  }
  if (!out_path) {
    print_usage(argv[0]);
    return 1;
  }
  if (!explicit_layer && !rain) config.ambient_gain[NOISE_KIND_PINK] = 0.3f;
  if (rain && !explicit_intensity) {
    config.rain_intensity = config.vary_rain ?
        0.5f * (config.min_rain_intensity + config.max_rain_intensity) : 0.5f;
  }
  double max_frames = (UINT32_MAX - 36u) / WAV_FRAME_BYTES;
  double requested_frames = seconds * NOISE_SAMPLE_RATE_HZ;
  if (!isfinite(requested_frames) || requested_frames < 1.0 || requested_frames > max_frames) {
    fprintf(stderr, "duration outside WAV range\n");
    return 1;
  }
  uint32_t frames = (uint32_t)requested_frames;
  static noise_gen gen;
  if (noise_init(&gen, &config, seed) != NOISE_OK) {
    fprintf(stderr, "invalid configuration; check intensity bounds and initial intensity\n");
    return 1;
  }
  FILE *file = fopen(out_path, "wb");
  if (!file) {
    perror(out_path);
    return 1;
  }
  if (!write_wav_header(file, frames)) {
    fprintf(stderr, "cannot write header to %s\n", out_path);
    fclose(file);
    return 1;
  }
  int16_t batch[HOST_BATCH * NOISE_CHANNELS];
  unsigned char bytes[HOST_BATCH * WAV_FRAME_BYTES];
  uint32_t remaining = frames;
  clock_t start = clock();
  while (remaining) {
    size_t count = remaining < HOST_BATCH ? remaining : HOST_BATCH;
    noise_fill(&gen, batch, count);
    for (size_t i = 0; i < count * NOISE_CHANNELS; ++i) {
      uint16_t sample = (uint16_t)batch[i];
      bytes[2 * i] = (unsigned char)(sample & 0xffu);
      bytes[2 * i + 1] = (unsigned char)(sample >> 8);
    }
    if (fwrite(bytes, WAV_FRAME_BYTES, count, file) != count) {
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
  double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
  printf("%s: %u stereo frames, seed %" PRIu32 ", %.3f CPU seconds\n"
         "engine: %zu bytes; peak voices: %u/%u; drops: %" PRIu64
         "; capacity losses: %" PRIu64 "; clipped samples: %" PRIu64 "\n",
         out_path, frames, seed, elapsed, sizeof(gen), gen.state.peak_active_drops,
         NOISE_MAX_DROPLETS, gen.state.generated_drops, gen.state.dropped_drops,
         gen.state.clipped_samples);
  return 0;
}
