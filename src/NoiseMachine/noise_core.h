#ifndef NOISE_CORE_H
#define NOISE_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NOISE_SAMPLE_RATE_HZ 44100u
#define NOISE_CHANNELS 2u
#define NOISE_MAX_DROPLETS 128u
#define NOISE_REVERB_LINES 6u
#define NOISE_REVERB_SAMPLES 7304u
#define NOISE_DIRECT_SAMPLES 128u
#define NOISE_CRICKET_VOICES 4u
#define NOISE_CICADA_VOICES 4u
#define NOISE_THUNDER_VOICES 2u
#define NOISE_THUNDER_SEGMENTS 256u
#define NOISE_THUNDER_BANDS 4u /* Three span the channel's range; one holds echoes. */
#define NOISE_THUNDER_ECHOES 6u
#define NOISE_THUNDER_REVERB_SAMPLES 6132u
#define NOISE_HUM_TABLE_SAMPLES 882u /* One 50 Hz period. */

typedef enum noise_kind {
  NOISE_KIND_WHITE = 0,
  NOISE_KIND_PINK,
  NOISE_KIND_HUM_50HZ,
  NOISE_KIND_HUM_60HZ,
  NOISE_KIND_COUNT
} noise_kind;

typedef enum impact_surface {
  WATER = 0,
  DIRT,
  LEAF,
  CONCRETE,
  GLASS,
  METAL,
  PLASTIC,
  ASPHALT,
  ASPHALT_ROOF,
  NOISE_SURFACE_COUNT
} impact_surface;

typedef enum weather_mod_destination {
  WEATHER_MOD_ARRIVAL_RATE = 0,
  WEATHER_MOD_DROP_SIZE,
  WEATHER_MOD_RAIN_GAIN,
  WEATHER_MOD_REVERB_GAIN,
  WEATHER_MOD_FALL_HEIGHT,
  WEATHER_MOD_MIN_DISTANCE,
  WEATHER_MOD_MAX_DISTANCE,
  WEATHER_MOD_WATER_WEIGHT,
  WEATHER_MOD_DIRT_WEIGHT,
  WEATHER_MOD_LEAF_WEIGHT,
  WEATHER_MOD_CONCRETE_WEIGHT,
  WEATHER_MOD_GLASS_WEIGHT,
  WEATHER_MOD_METAL_WEIGHT,
  WEATHER_MOD_PLASTIC_WEIGHT,
  WEATHER_MOD_ASPHALT_WEIGHT,
  WEATHER_MOD_ASPHALT_ROOF_WEIGHT,
  NOISE_WEATHER_MOD_COUNT
} weather_mod_destination;

typedef enum cicada_species {
  CICADA_DOG_DAY = 0,
  CICADA_MINMINZEMI,
  CICADA_HIGURASHI,
  NOISE_CICADA_SPECIES_COUNT
} cicada_species;

typedef enum noise_result {
  NOISE_OK = 0,
  NOISE_INVALID_CONFIG,
  NOISE_INVALID_DROP,
  NOISE_VOICE_LIMIT,
  NOISE_INVALID_STRIKE
} noise_result;

typedef struct position_polar {
  float distance_m;
  float angle_rad; /* 0 front, pi/2 right, pi behind, 3pi/2 left. */
} position_polar;

typedef struct droplet {
  impact_surface surface;
  float radius_m;
  float velocity_m_s;
  float bubble_radius_m; /* Zero disables the bubble; nonzero requires WATER. */
  position_polar position;
} droplet;

typedef struct thunder_strike {
  position_polar position; /* Distance 200..15000 m. */
} thunder_strike;

/* Crickets and cicadas share this placement; each voice keeps fixed offsets within it. */
typedef struct noise_placement {
  float stereo_width; /* Angular spread: 0 all in front, 1 all around. */
  float min_distance_m;
  float max_distance_m;
} noise_placement;

typedef struct noise_listener_config {
  float stereo_width_m; /* Ear spacing; sphere radius is half this width. */
  float head_amount; /* 0: spaced microphones, 1: spherical head. */
  float rear_amount; /* 0: bypass rear filter, 1: full rear filter. */
} noise_listener_config;

typedef struct noise_weather_config {
  int vary;
  float intensity; /* Initial intensity, 0..1; zero means no arrivals. */
  float min_intensity;
  float max_intensity;
  float step_s;
  float slew_s;
  float mod_amount[NOISE_WEATHER_MOD_COUNT]; /* Bipolar depths, -1..1. */
} noise_weather_config;

typedef struct noise_water_config {
  float impact_gain_min;
  float impact_gain_max;
  float bubble_probability;
  float bubble_radius_min_m;
  float bubble_radius_max_m;
  float bubble_gain_min;
  float bubble_gain_max;
  float bubble_decay_min;
  float bubble_decay_max;
} noise_water_config;

typedef struct noise_rain_config {
  float gain;
  float max_drops_per_s;
  float surface_weight[NOISE_SURFACE_COUNT]; /* Nonnegative relative weights. */
  float min_distance_m;
  float max_distance_m;
  float fall_height_m;
  noise_water_config water;
} noise_rain_config;

typedef struct noise_wind_config {
  float gain;
  float brightness;
  float gust_depth;
  float gust_rate_hz;
  float stereo_width;
} noise_wind_config;

typedef struct noise_cricket_config {
  float gain;
  float call_rate_hz;
  float pitch_hz;
  float pitch_variation;
  noise_placement placement;
} noise_cricket_config;

typedef struct noise_cicada_config {
  float gain;
  cicada_species species;
  float pitch_hz;
  float click_rate_scale; /* 0.5..1.5 times the species' tymbal click rate. */
  float chorus; /* Level of the distant chorus under the individuals. */
  noise_placement placement;
} noise_cicada_config;

typedef struct noise_thunder_config {
  float gain;
  float rate_per_min; /* Automatic strikes; zero allows only manual strikes. */
  float min_distance_m;
  float max_distance_m;
  float reverb_gain;
  float reverb_decay_s; /* Time to fall 60 dB. */
} noise_thunder_config;

typedef struct noise_config {
  float master_gain;
  float ambient_gain[NOISE_KIND_COUNT]; /* Independent linear gains, each 0..1. */
  float reverb_gain; /* Rain and insect sends are before distance attenuation. */
  noise_listener_config listener;
  noise_weather_config weather;
  noise_rain_config rain;
  noise_wind_config wind;
  noise_cricket_config crickets;
  noise_cicada_config cicadas;
  noise_thunder_config thunder;
} noise_config;

typedef struct noise_mode {
  float previous;
  float current;
  float coefficient;
  float radius_squared;
  uint32_t remaining;
  uint32_t delay;
} noise_mode;

typedef struct noise_oscillator {
  float previous;
  float current;
  float coefficient;
} noise_oscillator;

/* Distance, ear delay, head shadow, and rear filter for one point source. */
typedef struct noise_spatial {
  float ear_gain[2];
  unsigned ear_delay[2];
  float delay_weight[2][4];
  float head_b0[2];
  float head_b1[2];
  float head_feedback;
  float head_state[2];
  float head_previous_input;
  float lowpass_alpha;
  float lowpass_state;
} noise_spatial;

/* One persistent cricket; its offsets scale with the live config at each chirp. */
typedef struct noise_cricket_voice {
  noise_oscillator oscillator;
  noise_spatial spatial;
  float glide; /* Oscillator coefficient step per frame within a pulse. */
  float pitch_offset; /* -1..1 */
  float angle_offset; /* -1..1, times pi times stereo width. */
  float distance_offset; /* 0..1, area-uniform between the distance bounds. */
  float period_scale; /* Chirp period relative to 1 / call rate. */
  unsigned pulses; /* Per chirp. */
  unsigned singing;
  uint32_t pulse_samples;
  uint32_t sounding_samples;
  uint32_t chirp_samples; /* Frames since the chirp started. */
  uint32_t until_chirp;
  uint32_t bout_samples; /* Frames left in the singing or silent bout. */
} noise_cricket_voice;

/* Two-pole band-pass: y = coefficient y1 - radius^2 y2 + x - x2. */
typedef struct noise_resonator {
  float coefficient;
  float radius_squared;
  float state[2];
  float input[2];
} noise_resonator;

/* One persistent cicada; its offsets scale with the live config at each call. */
typedef struct noise_cicada_voice {
  noise_resonator body; /* Abdomen resonance rung by each tymbal click. */
  noise_oscillator throb;
  noise_spatial spatial;
  float glide; /* Body coefficient step per frame while the pitch moves. */
  float pitch_offset; /* -1..1 */
  float angle_offset; /* -1..1, times pi times stereo width. */
  float distance_offset; /* 0..1, area-uniform between the distance bounds. */
  float until_click; /* Frames. */
  unsigned syllable; /* Index in the phrase. */
  unsigned syllables; /* In the phrase; the held note follows the last. */
  unsigned holding; /* The current note is the held note. */
  uint32_t note_samples; /* Frames into the current syllable or held note. */
  uint32_t note_length; /* Zero while silent. */
  uint32_t sounding; /* Frames of the note that sound; the rest is a gap. */
  uint32_t until_call;
} noise_cicada_voice;

typedef struct noise_thunder_segment {
  float start; /* Frames after the strike's first arrival. */
  float width; /* Arrival spread between the segment's ends, frames. */
  float gain[2]; /* Per channel, divided by width. */
  float roughness; /* Fine-tortuosity noise per sqrt(frame), relative to gain. */
  float band; /* Direct range position, 0 to 2; fractions blend neighbours. */
} noise_thunder_segment;

typedef struct noise_biquad {
  float b0, b1, b2, a1, a2;
  float state[2];
} noise_biquad;

typedef struct noise_reflector {
  float position[2]; /* Ground point, x right and y front, metres. */
  float reflectivity; /* Pressure ratio. */
  float smear_s; /* Arrival spread added by terrain roughness. */
} noise_reflector;

typedef struct noise_thunder_echo {
  float delay; /* Frames after the direct arrival. */
  float smear; /* Frames added to each segment's arrival spread. */
  float range_log; /* Log of echo path over direct path. */
  float gain[2];
  unsigned first;
  unsigned next;
} noise_thunder_echo;

typedef struct noise_thunder_voice {
  noise_thunder_segment segment[NOISE_THUNDER_SEGMENTS]; /* Sorted by start. */
  unsigned segments;
  unsigned first; /* Segments before this index have ended. */
  unsigned next; /* Segments from this index have not arrived. */
  uint32_t elapsed;
  uint32_t length; /* Zero marks a free voice. */
  /* Per range band: band-pass at 1/period shapes excitation into N-waves, then a
     fourth-order Butterworth air low-pass per channel. */
  noise_biquad pulse[NOISE_THUNDER_BANDS][2];
  noise_biquad air[NOISE_THUNDER_BANDS][2][2];
  noise_thunder_echo echo[NOISE_THUNDER_ECHOES];
  float span_log; /* Log of farthest over nearest direct segment range. */
  float echo_span_log; /* Log of the widest echo path ratio; band 3 sits there. */
} noise_thunder_voice;

typedef struct noise_drop_voice {
  noise_mode mode[3];
  noise_spatial spatial;
  float material_lowpass_alpha;
  float material_lowpass_state[2];
  unsigned filter_tail;
} noise_drop_voice;

typedef struct noise_state {
  float rain_intensity;
  float rain_target;
  unsigned weather_state;
  unsigned active_drops;
  unsigned peak_active_drops;
  uint64_t generated_drops;
  uint64_t dropped_drops;
  uint64_t clipped_samples;
  uint64_t generated_thunder;
  uint64_t dropped_thunder;
} noise_state;

/* Direct-path mix that point sources write ahead into at their ear delays. */
typedef struct noise_bus {
  float direct[2][NOISE_DIRECT_SAMPLES];
  unsigned position;
} noise_bus;

/* Six-line feedback delay network; the caller owns the delay-line buffer. */
typedef struct noise_fdn {
  unsigned position[NOISE_REVERB_LINES];
  float damping[NOISE_REVERB_LINES];
  float feedback[NOISE_REVERB_LINES];
} noise_fdn;

typedef struct noise_reverb {
  float buffer[NOISE_REVERB_SAMPLES];
  noise_fdn fdn;
} noise_reverb;

typedef struct noise_ambient {
  uint32_t rng;
  float pink[7];
  uint32_t hum_sample;
  float hum_table[NOISE_HUM_TABLE_SAMPLES];
} noise_ambient;

typedef struct noise_wind {
  uint32_t rng;
  float air[2];
  float rumble[2];
  float gust;
  float gust_target;
  float air_alpha;
  float gust_alpha;
  float brightness_cache;
  float gust_rate_cache;
  uint32_t gust_samples;
} noise_wind;

typedef struct noise_crickets {
  uint32_t rng;
  unsigned started;
  noise_cricket_voice voice[NOISE_CRICKET_VOICES];
} noise_crickets;

typedef struct noise_cicadas {
  uint32_t rng;
  unsigned started;
  noise_cicada_voice voice[NOISE_CICADA_VOICES];
  noise_resonator chorus[2]; /* Independent per ear. */
  int species_cache;
  float pitch_cache;
  float swell;
  float swell_target;
  uint32_t swell_samples;
} noise_cicadas;

/* Runs at a quarter of the sample rate and interpolates its output. */
typedef struct noise_thunder_reverb {
  float buffer[NOISE_THUNDER_REVERB_SAMPLES];
  noise_fdn fdn;
  float decay_cache;
  float input;
  unsigned phase;
  float output[2][2]; /* Previous and current quarter-rate outputs. */
} noise_thunder_reverb;

typedef struct noise_thunder {
  uint32_t rng;
  uint32_t echo_rng;
  unsigned started;
  noise_thunder_voice voice[NOISE_THUNDER_VOICES];
  noise_reflector reflector[NOISE_THUNDER_ECHOES]; /* Fixed per seed: every strike echoes off the same terrain. */
  noise_thunder_reverb reverb;
} noise_thunder;

typedef struct noise_weather {
  uint32_t rng;
  uint32_t samples;
  uint32_t period;
  float slew;
  float slew_error;
} noise_weather;

typedef struct noise_rain {
  uint32_t arrival_rng;
  uint32_t drop_rng;
  float surface_cdf[NOISE_SURFACE_COUNT];
  noise_drop_voice voice[NOISE_MAX_DROPLETS];
} noise_rain;

/* Caller-owned storage. Treat all fields except the read-only state as private. */
typedef struct noise_gen {
  noise_config config;
  noise_state state;
  noise_bus bus;
  noise_ambient ambient;
  noise_wind wind;
  noise_crickets crickets;
  noise_cicadas cicadas;
  noise_thunder thunder;
  noise_weather weather;
  noise_rain rain;
  noise_reverb reverb;
} noise_gen;

void noise_config_default(noise_config *config);
/* Rejects invalid values without modifying gen. Seed zero aliases seed one. */
noise_result noise_init(noise_gen *gen, const noise_config *config, uint32_t seed);
/* Starts an arrival at the listener. Call between fills on the audio thread. */
noise_result noise_trigger_drop(noise_gen *gen, const droplet *drop);
/* Starts a strike at the listener. Call between fills on the audio thread. */
noise_result noise_trigger_thunder(noise_gen *gen, const thunder_strike *strike);
/* Writes 2 * frames interleaved int16 samples (L, R); returns frames. */
size_t noise_fill(noise_gen *gen, int16_t *out, size_t frames);

#ifdef __cplusplus
}
#endif

#endif
