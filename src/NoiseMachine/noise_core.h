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

typedef enum noise_kind {
  NOISE_KIND_WHITE = 0,
  NOISE_KIND_PINK,
  HUM_50HZ,
  HUM_60HZ,
  NOISE_KIND_WIND,
  NOISE_KIND_CRICKETS,
  NOISE_KIND_CICADAS,
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

typedef struct noise_config {
  float ambient_gain[NOISE_KIND_COUNT]; /* Independent linear gains, each 0..1. */
  float wind_brightness;
  float wind_gust_depth;
  float wind_gust_rate_hz;
  float wind_stereo_width;
  float cricket_call_rate_hz;
  float cricket_pitch_hz;
  float cricket_pitch_variation;
  float cricket_stereo_width; /* Angular spread: 0 all in front, 1 all around. */
  float cricket_min_distance_m;
  float cricket_max_distance_m;
  cicada_species cicada_species;
  float cicada_pitch_hz;
  float cicada_click_rate_scale; /* 0.5..1.5 times the species' tymbal click rate. */
  float cicada_chorus; /* Level of the distant chorus under the individuals. */
  float cicada_stereo_width; /* Angular spread: 0 all in front, 1 all around. */
  float cicada_min_distance_m;
  float cicada_max_distance_m;
  float thunder_gain;
  float thunder_rate_per_min; /* Automatic strikes; zero allows only manual strikes. */
  float thunder_min_distance_m;
  float thunder_max_distance_m;
  float thunder_reverb_gain;
  float thunder_reverb_decay_s; /* Time to fall 60 dB. */
  float master_gain;
  float rain_gain;
  float rain_intensity; /* Initial intensity, 0..1; zero means no arrivals. */
  float min_rain_intensity;
  float max_rain_intensity;
  int vary_rain;
  float weather_step_s;
  float rain_slew_s;
  float max_drops_per_s;
  float surface_weight[NOISE_SURFACE_COUNT]; /* Nonnegative relative weights. */
  float min_distance_m;
  float max_distance_m;
  float fall_height_m;
  float stereo_width_m; /* Ear spacing; sphere radius is half this width. */
  float head_amount; /* 0: spaced microphones, 1: spherical head. */
  float rear_amount; /* 0: bypass rear filter, 1: full rear filter. */
  float reverb_gain; /* Rain and insect sends are before distance attenuation. */
  float water_impact_gain_min;
  float water_impact_gain_max;
  float water_bubble_probability;
  float water_bubble_radius_min_m;
  float water_bubble_radius_max_m;
  float water_bubble_gain_min;
  float water_bubble_gain_max;
  float water_bubble_decay_min;
  float water_bubble_decay_max;
  float weather_mod_amount[NOISE_WEATHER_MOD_COUNT]; /* Bipolar depths, -1..1. */
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

/* Caller-owned storage. Treat all fields except the read-only state as private. */
typedef struct noise_gen {
  noise_config config;
  noise_state state;
  uint32_t ambient_rng;
  uint32_t wind_rng;
  uint32_t cricket_rng;
  uint32_t cicada_rng;
  uint32_t thunder_rng;
  uint32_t thunder_echo_rng;
  uint32_t arrival_rng;
  uint32_t drop_rng;
  uint32_t weather_rng;
  float pink_b[7];
  float wind_filter[2];
  float wind_rumble[2];
  float wind_gust;
  float wind_gust_target;
  float wind_air_alpha;
  float wind_gust_alpha;
  float wind_brightness_cache;
  float wind_gust_rate_cache;
  uint32_t wind_gust_samples;
  noise_cricket_voice crickets[NOISE_CRICKET_VOICES];
  unsigned cricket_started;
  noise_cicada_voice cicadas[NOISE_CICADA_VOICES];
  unsigned cicada_started;
  noise_resonator cicada_chorus[2]; /* Independent per ear. */
  int cicada_species_cache;
  float cicada_pitch_cache;
  float cicada_swell;
  float cicada_swell_target;
  uint32_t cicada_swell_samples;
  noise_thunder_voice thunder[NOISE_THUNDER_VOICES];
  noise_reflector reflector[NOISE_THUNDER_ECHOES]; /* Fixed per seed: every strike echoes off the same terrain. */
  unsigned thunder_started;
  float thunder_reverb[NOISE_THUNDER_REVERB_SAMPLES];
  unsigned thunder_reverb_position[NOISE_REVERB_LINES];
  float thunder_reverb_damping[NOISE_REVERB_LINES];
  float thunder_reverb_feedback[NOISE_REVERB_LINES];
  float thunder_reverb_decay_cache;
  float thunder_reverb_input;
  unsigned thunder_reverb_phase;
  float thunder_reverb_output[2][2]; /* Previous and current quarter-rate outputs. */
  uint32_t hum_sample;
  float hum_table[882];
  noise_drop_voice voices[NOISE_MAX_DROPLETS];
  float surface_cdf[NOISE_SURFACE_COUNT];
  uint32_t weather_samples;
  uint32_t weather_period;
  float rain_slew;
  float rain_slew_error;
  float reverb[NOISE_REVERB_SAMPLES];
  unsigned reverb_position[NOISE_REVERB_LINES];
  float reverb_damping[NOISE_REVERB_LINES];
  float reverb_feedback[NOISE_REVERB_LINES];
  float direct[2][NOISE_DIRECT_SAMPLES];
  unsigned direct_position;
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
