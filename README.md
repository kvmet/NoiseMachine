# NoiseMachine

A portable C synthesis engine for ambient noise and rain, with a desktop WAV
renderer and an Arduino sketch for eventual ESP32 audio output.

- Mix white noise, pink noise, 50 Hz hum, 60 Hz hum, wind, crickets, and cicadas.
- Generate individual rain impacts on water, dirt, leaves, concrete, glass,
  metal, plastic, asphalt, and asphalt roofs, with water-bubble resonance and
  Marshall-Palmer drop sizes from drizzle to a 200 mm/h deluge.
- Simulate passing storms: rain, gusty wind, temperature, and lightning that
  drive the rain, wind, insects, and thunder, with a time speed-up for testing.
- Place drops around the listener with per-ear attenuation, fractional delay,
  a tunable spherical-head HRTF, rear filtering, and shared stereo reverb.
- Place crickets and cicadas (ten species from Japan, the US, France, and
  Australia) around the listener with the same spatial model and reverb as rain.
- Trigger thunder strikes by hand or from the storm's lightning. Each strike sums
  N-waves from a random tortuous channel, filtered by distance, with echoes
  off fixed terrain and its own reverb.

The [model documentation](docs/index.md) gives the equations, units,
assumptions, default surfaces, API contract, and cited references.
Frogs are deferred.

## Build and listen

No added dependencies. The host needs a C11 compiler, make, and libm:

```sh
make -C host
mkdir -p out
./host/noise_host -k rain -k wind -k thunder -v -x 60 -d 120 out/storm.wav
afplay out/storm.wav
```

Use headphones to hear the binaural cues. WAV output is 44.1 kHz, stereo,
signed 16-bit PCM.

```sh
# Heavy rain on water, with a smaller virtual head.
./host/noise_host -k rain -r 40 -m water -b 0.16 -d 20 out/water.wav

# A deluge mixed with a hum layer.
./host/noise_host -k rain -k hum50 -r 150 -d 20 out/deluge-hum.wav

# Spaced microphones, head and rear filters disabled.
./host/noise_host -k rain -a 0 -f 0 -d 20 out/mics.wav

# Mono direct rain with no reverb.
./host/noise_host -k rain -b 0 -e 0 -d 20 out/mono-rain.wav

# Steady rain with thunder at six flashes per minute.
./host/noise_host -k rain -k thunder -r 20 -t 6 -d 60 out/thunder-rain.wav

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

Press **Start** for continuous playback. The tabs expose every field in
`noise_config`:

- **Mixer**: every level. Each weather-driven layer shows what the weather is
  doing to it, such as the played and physical drop rates, the bed's share,
  the wind level, or why the insects are silent. **Rain bed ×** balances the
  noise bed against the individual drops.
- **Storm**: simulated storms, or fixed weather while **Hold weather at fixed
  values** is checked; only the active group's controls respond.
- **Storm Shape**: how a storm's rain, gust front, cooling, lightning, and
  track depend on its severity.
- **Wind**: stereo width, brightness, rumble, lean toward the wind, and gusts.
- **Insects**: crickets and cicadas, including the weather each sings in.
- **Thunder**: reverb, strike scatter, and **Strike**, which starts one strike
  1 to 8 km away.
- **Rain**, **Impact**, **Bubbles**: the drop budget, distances, gust sheet
  depth, and the list of up to nine named surfaces. Pick a surface to rename
  it, set its share of the ground, mark it **Vertical** so wind-driven rain
  hits it, or delete it; **Add** appends a copy.
- **Spatial**: listener width, head, and rear filter.

Two lines under the tabs show the current weather and which layers it
silences. The seed and **Reset generator** sit beside **Start**. **Export…**
renders the current settings to an AAC `.m4a` file of a chosen length. Type an
exact value or use a slider;
wide physical ranges use logarithmic sliders. Changes apply while audio plays.
Distance, head, width, and surface changes affect new rain drops; existing drop
tails finish with their original spatial settings. Changing the seed and
resetting the generator clears all current audio state.

## Host controls

Run `./host/noise_host -h` for usage.

- `-k white|pink|hum50|hum60|wind|crickets|cicadas|rain|thunder`: select a layer; repeat
  to mix. Each layer has gain 0.5. With no layer or
  rain option, default to pink.
- `-r MM_H`: fixed rain rate from 0 to 200 mm/h. Enables rain even without
  `-k rain`. Default with rain: 10.
- `-w M_S`: fixed mean wind from 0 to 40 m/s. Default: 10 with `-k wind`,
  otherwise 3.
- `-t NUMBER`: fixed lightning flashes per minute, from 0 to 30. Default: 2
  with `-k thunder`. The first strike starts at time zero.
- `-T CELSIUS`: fixed temperature from -10 to 45. Default: 25.
- `-v`: simulate passing storms instead of fixed weather. Enables rain.
- `-x NUMBER`: storm time speed-up from 1 to 600. Default: 1.
- `-m mixed|water|dirt|leaf|concrete|glass|metal|plastic|asphalt|asphalt-roof`:
  rain surface, matched against the default surface names ignoring case, with a
  hyphen for a space. Default: mixed. The mix is 37% water, 21% dirt, 26% leaves,
  15% concrete, and 0.5% each glass and metal. Plastic, asphalt, and roof
  coverage defaults to zero.
- `-n NUMBER`: drops played one by one per second, from 0 to 2000. A noise
  bed plays the rest. Default: 900.
- `-c SPECIES`: cicada species, which also sets its typical pitch. `-h`
  lists them. Default: dog-day.
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
thunder strikes and rejections, and clipped channel samples. Reduce rate if the voice pool fills, or reduce
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
config.rain.gain = 0.5f;
config.storm.manual = 0; /* Simulate passing storms. */
if (noise_init(&generator, &config, 1) != NOISE_OK) {
  /* Report invalid configuration to the caller. */
  return;
}
noise_fill(&generator, frames, 256);
```

Configuration also exposes individual layer gains, up to nine named surfaces,
source distance bounds, fixed weather, and the storm climate.
`noise_trigger_drop` accepts an individual drop with radius, speed, surface index,
bubble radius, and polar position. `noise_trigger_thunder` accepts a strike
distance and angle. See [the API and limits](docs/index.md#api-and-limits).

The engine owns no heap memory and renders into caller-owned buffers. It
currently occupies about 105 KB plus output buffers. Keep it in static storage.
`noise_set_config` applies changes while playing; control and render calls
must share one thread. The API counts stereo frames, so allocate two samples per
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
