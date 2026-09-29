# NoiseMachine

A portable C synthesis engine for ambient noise and rain, with a desktop WAV
renderer and an Arduino sketch for eventual ESP32 audio output.

- Mix white noise, pink noise, 50 Hz hum, and 60 Hz hum.
- Generate individual rain impacts on water, dirt, leaves, concrete, glass,
  and metal, with water-bubble resonance and optional Markov intensity changes.
- Place drops around the listener with per-ear attenuation, fractional delay,
  a tunable spherical-head HRTF, rear filtering, and shared stereo reverb.

The [model documentation](docs/index.md) gives the equations, units,
assumptions, material presets, API contract, and cited references.
Wind and thunder are deferred.

## Build and listen

No added dependencies. The host needs a C11 compiler, make, and libm:

```sh
make -C host
mkdir -p out
./host/noise_host -k rain -v -d 60 out/rain.wav
afplay out/rain.wav
```

Use headphones to hear the binaural cues. WAV output is 44.1 kHz, stereo,
signed 16-bit PCM.

```sh
# Fixed rain on water, with a smaller virtual head.
./host/noise_host -k rain -r 0.6 -m water -b 0.16 -d 20 out/water.wav

# Rain mixed with a hum layer.
./host/noise_host -k rain -k hum50 -r 0.4 -d 20 out/rain-hum.wav

# Spaced microphones, head and rear filters disabled.
./host/noise_host -k rain -a 0 -f 0 -d 20 out/mics.wav

# Mono direct rain with no reverb.
./host/noise_host -k rain -b 0 -e 0 -d 20 out/mono-rain.wav

# Pink noise, or substitute white, hum50, or hum60.
./host/noise_host -k pink -d 10 out/pink.wav
```

`make -C host run` builds, renders pink noise, and plays it using macOS
`afplay`. Other platforms can play the WAV in any audio player.

## Live macOS GUI

Build and open the basic native GUI:

```sh
make -C host gui
./host/noise_gui
```

Press **Start** for continuous playback. The Mixer, Rain, Water, Weather Mod,
and Spatial tabs expose every field in `noise_config`. Water controls randomized
impact gain, bubble probability, radius, gain, and decay ranges. Weather Mod
routes intensity to density, size, gain, reverb, physical, and surface
parameters with bipolar attenuverters. Type an exact value or use a slider;
wide physical ranges use logarithmic sliders. Changes apply while audio plays.
Distance, head, width, and surface changes affect new rain drops; existing drop
tails finish with their original spatial settings. Changing the seed and
resetting the generator clears all current audio state.

## Host controls

Run `./host/noise_host -h` for usage.

- `-k white|pink|hum50|hum60|rain`: select a layer; repeat to mix. Each
  ambient layer has gain 0.3. With no layer or rain option, default to pink.
- `-r NUMBER`: fixed rain intensity from 0 to 1. Enables rain even without
  `-k rain`. Default rain intensity is 0.5.
- `-v`: enable Markov rain variation, initially at the bounds' midpoint
  unless `-r` supplies an initial value.
- `-l NUMBER`, `-u NUMBER`: lower and upper varying-intensity bounds.
  Defaults: 0.15 and 0.85. An explicit initial intensity must be within them.
- `-m mixed|water|dirt|leaf|concrete|glass|metal`: rain material. Default: mixed.
  The mix is 37% water, 21% dirt, 26% leaves, 15% concrete, and 0.5% each
  glass and metal.
- `-n NUMBER`: arrival rate at intensity one, from 0 to 2000/s. Default: 900.
- `-b METRES`: ear spacing and head diameter, from 0 to 0.5. Default: 0.18.
- `-a NUMBER`: head effect from 0 to 1. Default: 1. Zero retains geometric
  mic delay and attenuation while disabling shadowing and diffraction.
- `-f NUMBER`: rear filter strength from 0 to 1. Default: 1; zero bypasses it.
  At full strength, the cutoff ranges from 18 kHz in front to 3 kHz behind.
- `-e NUMBER`: reverb gain from 0 to 1. Default: 0.12.
- `-g NUMBER`: master gain from 0 to 1. Default: 0.8.
- `-s INTEGER`: 32-bit seed. Default: 1; zero aliases one.
- `-d SECONDS`: duration. Default: 10.

The renderer reports CPU time, engine memory, peak voices, rejected drops,
and clipped channel samples. Reduce rate if the voice pool fills, or reduce
gain if clipping occurs. File output ends at the requested duration; it does
not append a reverb tail or fade. The same seed and settings reproduce output
within the same build, regardless of audio buffer size.

## Shared C API

```c
static noise_gen generator;
static int16_t frames[256 * NOISE_CHANNELS];
noise_config config;
noise_config_default(&config);
config.ambient_gain[NOISE_KIND_PINK] = 0.0f;
config.rain_intensity = 0.5f;
config.vary_rain = 1;
config.stereo_width_m = 0.18f;
config.head_amount = 1.0f;
if (noise_init(&generator, &config, 1) != NOISE_OK) {
  /* Report invalid configuration to the caller. */
  return;
}
noise_fill(&generator, frames, 256);
```

Configuration also exposes individual layer gains, material weights, source
distance bounds, falling height, weather interval, and smoothing time.
`noise_trigger_drop` accepts an individual drop with radius, speed, material,
bubble radius, and polar position. See [the API and limits](docs/index.md#api-and-limits).

The engine owns no heap memory and renders into caller-owned buffers. It
currently occupies about 52 KB plus output buffers. Keep it in static storage.
Configuration changes require reinitialization; control and render calls must
share one thread. The API counts stereo frames, so allocate two samples per
frame.

## Tests and ESP32 status

```sh
python3 tests/check.py
```

The checks use Python's standard library and the system C/C++ compilers.
They verify synthesis, stereo geometry and filtering, deterministic rendering,
WAV output, CLI validation, and the sketch's C/C++ linkage.

The sketch generates stereo buffers but still needs board-specific I2S setup,
pins, and an audio sink. A blocking writer or audio task must pace output at
44.1 kHz. Actual ESP32 build, RAM placement, and render deadlines remain to be
verified with the selected board and Arduino core. The head model is analytic;
measured HRTFs, pinna detail, elevation, and head tracking are not implemented.
