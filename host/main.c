#include <ctype.h>
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
      "  -k white|pink|hum50|hum60|wind|crickets|cicadas|rain|thunder   repeat to mix\n"
      "  -r mm/h        fixed rain rate, 0..200; default 10 with -k rain\n"
      "  -w m/s         fixed wind speed, 0..40; default 10 with -k wind\n"
      "  -t rate        fixed lightning flashes/minute, 0..30; default 2 with -k thunder\n"
      "  -T celsius     fixed temperature, -10..45; default 25\n"
      "  -v             simulate passing storms instead of fixed weather\n"
      "  -x scale       storm time speed-up, 1..600; default 1\n"
      "  -m mixed|water|dirt|leaf|concrete|glass|metal|plastic|asphalt|asphalt-roof\n"
      "  -c dog-day|minminzemi|higurashi   cicada species; default dog-day\n"
      "  -n rate        drops played one by one per second, 0..2000\n"
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

/* Case-insensitive; a hyphen in the argument matches a space in the name. */
static int surface_name_matches(const char *name, const char *arg) {
  for (;; ++name, ++arg) {
    int n = tolower((unsigned char)*name);
    int a = *arg == '-' ? ' ' : tolower((unsigned char)*arg);
    if (n != a) return 0;
    if (!n) return 1;
  }
}

int main(int argc, char **argv) {
  noise_config config;
  noise_config_default(&config);
  memset(config.ambient_gain, 0, sizeof(config.ambient_gain));
  uint32_t seed = 1;
  double seconds = 10.0;
  const char *out_path = NULL;
  int explicit_layer = 0;
  int rain = 0;
  int explicit_rain = 0, explicit_wind = 0, explicit_lightning = 0;
  /* Numeric options that store one config field. */
  const struct {
    const char *flag;
    double minimum, maximum;
    float *field;
    int *explicit_flag;
  } numbers[] = {
    {"-r", 0.0, 200.0, &config.storm.fixed.rain_mm_h, &explicit_rain},
    {"-w", 0.0, 40.0, &config.storm.fixed.wind_m_s, &explicit_wind},
    {"-t", 0.0, 30.0, &config.storm.fixed.lightning_per_min, &explicit_lightning},
    {"-T", -10.0, 45.0, &config.storm.fixed.temperature_c, NULL},
    {"-x", 1.0, 600.0, &config.storm.time_scale, NULL},
    {"-n", 0.0, 2000.0, &config.rain.max_drops_per_s, NULL},
    {"-b", 0.0, 0.5, &config.listener.stereo_width_m, NULL},
    {"-a", 0.0, 1.0, &config.listener.head_amount, NULL},
    {"-f", 0.0, 1.0, &config.listener.rear_amount, NULL},
    {"-e", 0.0, 1.0, &config.reverb_gain, NULL},
    {"-g", 0.0, 1.0, &config.master_gain, NULL},
  };

  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (strcmp(arg, "-h") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    if (strcmp(arg, "-v") == 0) {
      config.storm.manual = 0;
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
      } else if (strcmp(value, "thunder") == 0) {
        config.thunder.gain = 0.5f;
      } else if (strcmp(value, "wind") == 0) {
        config.wind.gain = 0.3f;
      } else if (strcmp(value, "crickets") == 0) {
        config.crickets.gain = 0.3f;
      } else if (strcmp(value, "cicadas") == 0) {
        config.cicadas.gain = 0.3f;
      } else {
        static const char *names[NOISE_KIND_COUNT] = {"white", "pink", "hum50", "hum60"};
        unsigned kind = 0;
        while (kind < NOISE_KIND_COUNT && strcmp(value, names[kind])) ++kind;
        if (kind == NOISE_KIND_COUNT) {
          fprintf(stderr, "unknown layer: %s\n", value);
          return 1;
        }
        config.ambient_gain[kind] = 0.3f;
      }
    } else if (strcmp(arg, "-m") == 0) {
      if (strcmp(value, "mixed") == 0) {
        noise_config defaults;
        noise_config_default(&defaults);
        for (unsigned i = 0; i < config.rain.surface_count; ++i) {
          config.rain.surface[i].coverage = defaults.rain.surface[i].coverage;
        }
      } else {
        unsigned match = 0;
        while (match < config.rain.surface_count &&
               !surface_name_matches(config.rain.surface[match].name, value)) {
          ++match;
        }
        if (match == config.rain.surface_count) {
          fprintf(stderr, "unknown surface: %s\n", value);
          return 1;
        }
        for (unsigned i = 0; i < config.rain.surface_count; ++i) {
          config.rain.surface[i].coverage = i == match ? 1.0f : 0.0f;
        }
      }
    } else if (strcmp(arg, "-c") == 0) {
      static const char *names[] = {"dog-day", "minminzemi", "higurashi"};
      unsigned species = 0;
      while (species < NOISE_CICADA_SPECIES_COUNT && strcmp(value, names[species])) ++species;
      if (species == NOISE_CICADA_SPECIES_COUNT) {
        fprintf(stderr, "unknown cicada species: %s\n", value);
        return 1;
      }
      config.cicadas.species = (cicada_species)species;
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
      size_t option = 0;
      while (option < sizeof(numbers) / sizeof(numbers[0]) && strcmp(arg, numbers[option].flag)) {
        ++option;
      }
      if (option == sizeof(numbers) / sizeof(numbers[0])) {
        fprintf(stderr, "unknown option: %s\n", arg);
        return 1;
      }
      if (number < numbers[option].minimum || number > numbers[option].maximum) {
        fprintf(stderr, "value for %s outside %g..%g\n", arg, numbers[option].minimum,
                numbers[option].maximum);
        return 1;
      }
      *numbers[option].field = (float)number;
      if (numbers[option].explicit_flag) *numbers[option].explicit_flag = 1;
      if (strcmp(arg, "-r") == 0) rain = 1;
    }
  }
  if (!out_path) {
    print_usage(argv[0]);
    return 1;
  }
  if (!explicit_layer && !rain) config.ambient_gain[NOISE_KIND_PINK] = 0.3f;
  if (rain && !explicit_rain) config.storm.fixed.rain_mm_h = 10.0f;
  if (config.wind.gain > 0.0f && !explicit_wind) config.storm.fixed.wind_m_s = 10.0f;
  if (config.thunder.gain > 0.0f && !explicit_lightning) {
    config.storm.fixed.lightning_per_min = 2.0f;
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
    fprintf(stderr, "invalid configuration\n");
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
         "; capacity losses: %" PRIu64 "; thunder strikes: %" PRIu64
         "; thunder losses: %" PRIu64 "; clipped samples: %" PRIu64 "\n",
         out_path, frames, seed, elapsed, sizeof(gen), gen.state.peak_active_drops,
         NOISE_MAX_DROPLETS, gen.state.generated_drops, gen.state.dropped_drops,
         gen.state.generated_thunder, gen.state.dropped_thunder, gen.state.clipped_samples);
  return 0;
}
