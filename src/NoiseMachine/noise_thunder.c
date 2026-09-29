#include "noise_thunder.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "noise_internal.h"

#define THUNDER_REFERENCE_M 1000.0f
#define THUNDER_FINE_STEP_M 3.0f
#define THUNDER_ROUGHNESS 0.3f
#define THUNDER_END_FADE 0.4f /* Fraction of a part that fades toward a late-arriving end. */
#define THUNDER_DIRECT_BANDS 3u
/* Thunder reverb lines at quarter rate: 50 to 146 ms, echo spacing like terrain. */
#define THUNDER_REVERB_DECIMATION 4u
static const unsigned thunder_reverb_length[NOISE_REVERB_LINES] = {
  557, 719, 887, 1063, 1297, 1609
};
static const unsigned thunder_reverb_offset[NOISE_REVERB_LINES] = {
  0, 557, 1276, 2163, 3226, 4523
};

typedef struct thunder_build {
  noise_thunder_voice *voice;
  uint32_t *rng;
  float step_m; /* Mean segment length. */
  float centroid[3]; /* Weighted by weight * length until noise_thunder_start divides. */
  float centroid_weight;
} thunder_build;

enum { FADE_NONE, FADE_BOTH_ENDS, FADE_TIP_END };

int noise_thunder_config_valid(const noise_thunder_config *c) {
  return in_range(c->gain, 0.0f, 1.0f) &&
         in_range(c->reverb_gain, 0.0f, 1.0f) &&
         in_range(c->reverb_decay_s, 0.5f, 10.0f) &&
         in_range(c->scatter_m, 0.0f, 10000.0f);
}

void noise_thunder_config_default(noise_thunder_config *c) {
  c->reverb_gain = 0.5f;
  c->reverb_decay_s = 3.5f;
  c->scatter_m = 4000.0f;
}

void noise_thunder_init(noise_thunder *thunder, uint32_t seed) {
  thunder->rng = stream_seed(seed, 0x2545f491u);
  thunder->echo_rng = stream_seed(seed, 0x6a09e667u);
  uint32_t terrain_rng = stream_seed(seed, 0xbb67ae85u);
  for (unsigned i = 0; i < NOISE_THUNDER_ECHOES; ++i) {
    noise_reflector *reflector = &thunder->reflector[i];
    float distance = random_between(&terrain_rng, 300.0f, 2500.0f);
    float angle = 2.0f * NOISE_PI * random_unit(&terrain_rng);
    reflector->position[0] = distance * sinf(angle);
    reflector->position[1] = distance * cosf(angle);
    reflector->reflectivity = random_between(&terrain_rng, 0.3f, 0.6f);
    reflector->smear_s = random_between(&terrain_rng, 0.1f, 0.4f);
  }
}

void noise_thunder_configure(noise_thunder *thunder, const noise_thunder_config *c) {
  float rate = (float)NOISE_SAMPLE_RATE_HZ / THUNDER_REVERB_DECIMATION;
  for (unsigned i = 0; i < NOISE_REVERB_LINES; ++i) {
    thunder->reverb.fdn.feedback[i] =
        powf(0.001f, (float)thunder_reverb_length[i] / (c->reverb_decay_s * rate));
  }
}

static float length3(const float v[3]) {
  return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void add_thunder_segment(thunder_build *b, const float from[3], const float to[3],
                                float length_m, float weight) {
  noise_thunder_voice *voice = b->voice;
  float mid[3] = {0.5f * (from[0] + to[0]), 0.5f * (from[1] + to[1]),
                  0.5f * (from[2] + to[2])};
  float near = length3(from), far = length3(to);
  if (near > far) {
    float swap = near;
    near = far;
    far = swap;
  }
  float frames_per_m = NOISE_SAMPLE_RATE_HZ / 343.0f;
  noise_thunder_segment *segment = &voice->segment[voice->segments++];
  for (unsigned k = 0; k < 3; ++k) b->centroid[k] += weight * length_m * mid[k];
  b->centroid_weight += weight * length_m;
  segment->start = near * frames_per_m;
  segment->band = length3(mid); /* Range for now; noise_thunder_start maps it to a band. */
  /* Fine steps wander in range by a random walk, so even a side-on segment spreads. */
  float wander_m = 0.28f * sqrtf(length_m * THUNDER_FINE_STEP_M);
  float spread_m = far - near;
  segment->width = sqrtf(spread_m * spread_m + wander_m * wander_m) * frames_per_m;
  float amplitude = 8.9f * weight * length_m * THUNDER_REFERENCE_M / length3(mid) /
                    segment->width;
  float horizontal = sqrtf(mid[0] * mid[0] + mid[1] * mid[1]);
  float pan = 0.25f * NOISE_PI * (1.0f + (horizontal > 0.0f ? mid[0] / horizontal : 0.0f));
  segment->gain[0] = amplitude * cosf(pan);
  segment->gain[1] = amplitude * sinf(pan);
  /* Fine steps each leave a random pulse; at one per 3 m their Poisson sum over a frame
     has variance roughness^2 * overlap. */
  float steps_per_frame = length_m / (THUNDER_FINE_STEP_M * segment->width);
  segment->roughness = THUNDER_ROUGHNESS / sqrtf(steps_per_frame);
}

/* Random walk with Gaussian direction changes, pulled toward a preferred direction. */
static void walk_thunder(thunder_build *b, float position[3], float direction[3],
                         const float preferred[3], float path_m, float weight, int fade) {
  float travelled = 0.0f;
  unsigned from = b->voice->segments;
  while (travelled < path_m && b->voice->segments < NOISE_THUNDER_SEGMENTS) {
    float step = b->step_m * random_between(b->rng, 0.5f, 1.5f);
    /* Measured: mean direction change 16.0 degrees (Hill), mean lean 28 degrees. */
    for (unsigned k = 0; k < 3; ++k) {
      direction[k] += 0.25f * random_gaussian(b->rng) + 0.2f * preferred[k];
    }
    float norm = 1.0f / length3(direction);
    float next[3];
    for (unsigned k = 0; k < 3; ++k) {
      direction[k] *= norm;
      next[k] = position[k] + step * direction[k];
    }
    add_thunder_segment(b, position, next, step, weight);
    memcpy(position, next, sizeof(next));
    travelled += step;
  }
  /* An end whose arrivals are still getting later is where this part's sound stops.
     Parts meeting there at full strength would stop together as a cut. */
  unsigned to = b->voice->segments;
  if (fade == FADE_NONE || to - from < 2u) return;
  noise_thunder_segment *segment = b->voice->segment;
  unsigned near_root = from + 3u < to ? from + 3u : to - 1u;
  unsigned near_tip = to >= from + 4u ? to - 4u : from;
  int root_late = fade == FADE_BOTH_ENDS && segment[from].start > segment[near_root].start;
  int tip_late = segment[to - 1u].start > segment[near_tip].start;
  for (unsigned k = from; k < to; ++k) {
    float done = ((float)(k - from) + 0.5f) / (float)(to - from);
    float gain = fminf(1.0f, fminf(root_late ? done : 1.0f, tip_late ? 1.0f - done : 1.0f) /
                             THUNDER_END_FADE);
    segment[k].gain[0] *= gain;
    segment[k].gain[1] *= gain;
  }
}

static int compare_thunder_segments(const void *a, const void *b) {
  float left = ((const noise_thunder_segment *)a)->start;
  float right = ((const noise_thunder_segment *)b)->start;
  return (left > right) - (left < right);
}

noise_result noise_thunder_start(noise_thunder *thunder, noise_state *state,
                                  position_polar position) {
  noise_thunder_voice *voice = NULL;
  for (unsigned i = 0; i < NOISE_THUNDER_VOICES && !voice; ++i) {
    if (!thunder->voice[i].length) voice = &thunder->voice[i];
  }
  if (!voice) {
    ++state->dropped_thunder;
    return NOISE_VOICE_LIMIT;
  }
  memset(voice, 0, sizeof(*voice));
  ++state->generated_thunder;
  uint32_t *rng = &thunder->rng;
  float distance = position.distance_m;

  float height = random_between(rng, 1500.0f, 4000.0f);
  float main_path = 1.15f * height;
  float cloud_path = random_between(rng, 1500.0f, 5000.0f);
  unsigned branches = 1u + random_u32(rng) % 3u;
  float branch_at[3], branch_path[3];
  unsigned arms = 2u + random_u32(rng) % 2u;
  float total = main_path + (float)arms * cloud_path;
  for (unsigned i = 0; i < branches; ++i) {
    branch_at[i] = random_between(rng, 0.2f, 0.9f);
    branch_path[i] = random_between(rng, 200.0f, 1200.0f);
    total += branch_path[i];
  }
  for (unsigned i = 1; i < branches; ++i) {
    for (unsigned j = i; j > 0 && branch_at[j] < branch_at[j - 1]; --j) {
      float swap = branch_at[j];
      branch_at[j] = branch_at[j - 1];
      branch_at[j - 1] = swap;
    }
  }
  /* Size segments so the whole channel fits the pool with a little spare. */
  thunder_build build = {voice, rng, total / (0.95f * NOISE_THUNDER_SEGMENTS), {0.0f}, 0.0f};

  /* Listener at the origin, x right, y front, z up. */
  float point[3] = {distance * sinf(position.angle_rad),
                    distance * cosf(position.angle_rad), 0.0f};
  float direction[3] = {0.0f, 0.0f, 1.0f};
  const float up[3] = {0.0f, 0.0f, 1.0f};
  float branch_point[3][3], branch_direction[3][3];
  float walked = 0.0f;
  for (unsigned i = 0; i < branches; ++i) {
    walk_thunder(&build, point, direction, up, branch_at[i] * main_path - walked, 1.0f,
                 FADE_NONE);
    walked = branch_at[i] * main_path;
    memcpy(branch_point[i], point, sizeof(point));
    memcpy(branch_direction[i], direction, sizeof(direction));
  }
  walk_thunder(&build, point, direction, up, main_path - walked, 1.0f, FADE_TIP_END);

  /* In-cloud arms spread in heading, so some arm usually arrives after the channel top.
     They add incoherently, so each gets weight / sqrt(arms). */
  float heading = 2.0f * NOISE_PI * random_unit(rng);
  float top[3];
  memcpy(top, point, sizeof(top));
  for (unsigned arm = 0; arm < arms; ++arm) {
    float h = heading + 2.0f * NOISE_PI * ((float)arm + random_between(rng, -0.25f, 0.25f)) /
              (float)arms;
    float level[3] = {cosf(h), sinf(h), 0.0f};
    memcpy(point, top, sizeof(top));
    memcpy(direction, level, sizeof(level));
    walk_thunder(&build, point, direction, level, cloud_path, 0.6f / sqrtf((float)arms),
                 FADE_BOTH_ENDS);
  }

  for (unsigned i = 0; i < branches; ++i) {
    float outward = 2.0f * NOISE_PI * random_unit(rng);
    float down[3] = {0.64f * cosf(outward), 0.64f * sinf(outward), -0.77f};
    walk_thunder(&build, branch_point[i], branch_direction[i], down, branch_path[i], 0.4f,
                 FADE_BOTH_ENDS);
  }

  qsort(voice->segment, voice->segments, sizeof(voice->segment[0]),
        compare_thunder_segments);
  float first = voice->segment[0].start;
  float last = 0.0f;
  float nearest = voice->segment[0].band, farthest = nearest;
  for (unsigned i = 0; i < voice->segments; ++i) {
    voice->segment[i].start -= first;
    float end = voice->segment[i].start + voice->segment[i].width;
    if (end > last) last = end;
    nearest = fminf(nearest, voice->segment[i].band);
    farthest = fmaxf(farthest, voice->segment[i].band);
  }
  /* Direct bands are evenly spaced in log range. */
  float span = logf(farthest / nearest);
  float bands_per_log = span > 0.0f ? (THUNDER_DIRECT_BANDS - 1u) / span : 0.0f;
  for (unsigned i = 0; i < voice->segments; ++i) {
    voice->segment[i].band = bands_per_log * logf(voice->segment[i].band / nearest);
  }
  voice->span_log = span;

  /* Each reflector returns the whole strike, delayed by its extra path from the channel's
     centroid, from its own direction, with spherical spreading over the longer path. */
  float centre[3];
  for (unsigned k = 0; k < 3; ++k) centre[k] = build.centroid[k] / build.centroid_weight;
  float direct_m = length3(centre);
  float widest = 1.0f, latest = 0.0f;
  for (unsigned i = 0; i < NOISE_THUNDER_ECHOES; ++i) {
    const noise_reflector *reflector = &thunder->reflector[i];
    noise_thunder_echo *echo = &voice->echo[i];
    float ground[3] = {reflector->position[0], reflector->position[1], 0.0f};
    float out[3] = {centre[0] - ground[0], centre[1] - ground[1], centre[2]};
    float path_m = length3(out) + length3(ground);
    echo->delay = (path_m - direct_m) * NOISE_SAMPLE_RATE_HZ / 343.0f;
    echo->smear = reflector->smear_s * NOISE_SAMPLE_RATE_HZ;
    echo->range_log = logf(path_m / direct_m);
    float amplitude = reflector->reflectivity * direct_m / path_m;
    float pan = 0.25f * NOISE_PI * (1.0f + ground[0] / length3(ground));
    echo->gain[0] = amplitude * cosf(pan);
    echo->gain[1] = amplitude * sinf(pan);
    widest = fmaxf(widest, path_m / direct_m);
    latest = fmaxf(latest, echo->delay + echo->smear);
  }
  voice->echo_span_log = logf(widest);
  /* Later arrivals travel farther, so the tail is darker and its N-waves longer.
     N-waves last 6 to 14 ms at 1 km and lengthen with the fourth root of distance. */
  float period_1km_s = 0.001f * random_between(rng, 6.0f, 14.0f);
  for (unsigned band = 0; band < NOISE_THUNDER_BANDS; ++band) {
    float band_m = band < THUNDER_DIRECT_BANDS ?
        distance * expf(span * band / (THUNDER_DIRECT_BANDS - 1u)) :
        distance * expf(span + voice->echo_span_log);
    float period_s = period_1km_s * sqrtf(sqrtf(band_m / THUNDER_REFERENCE_M));
    float air_cutoff = fminf(6000.0f, fmaxf(150.0f, 1000.0f *
        powf(THUNDER_REFERENCE_M / band_m, 0.6f)));
    for (unsigned channel = 0; channel < 2; ++channel) {
      biquad_tune(&voice->pulse[band][channel], 1, 1.0f / period_s, 0.7f);
      biquad_tune(&voice->air[band][channel][0], 0, air_cutoff, 0.5411961f);
      biquad_tune(&voice->air[band][channel][1], 0, air_cutoff, 1.3065630f);
    }
  }
  /* 4096 frames let the filters ring out after the last arrival. */
  voice->length = (uint32_t)(last + latest) + 4096u;
  return NOISE_OK;
}

/* Adds a segment's box to the two bands nearest its range position. */
static void add_to_bands(float excitation[NOISE_THUNDER_BANDS][2], float position,
                         const float amount[2]) {
  position = fminf(fmaxf(position, 0.0f), NOISE_THUNDER_BANDS - 1.0f);
  unsigned band = (unsigned)position;
  if (band > NOISE_THUNDER_BANDS - 2u) band = NOISE_THUNDER_BANDS - 2u;
  float upper = position - (float)band;
  for (unsigned channel = 0; channel < 2; ++channel) {
    excitation[band][channel] += (1.0f - upper) * amount[channel];
    excitation[band + 1][channel] += upper * amount[channel];
  }
}

/* Echo paths inside the direct span use the direct bands; longer ones reach band 3. */
static float echo_band(const noise_thunder_voice *voice, float band, float range_log) {
  float span = voice->span_log;
  float u = band * span / (THUNDER_DIRECT_BANDS - 1u) + range_log;
  if (u <= span) return span > 0.0f ? (THUNDER_DIRECT_BANDS - 1u) * u / span : 0.0f;
  return (THUNDER_DIRECT_BANDS - 1u) + (u - span) / voice->echo_span_log;
}

static void thunder_voice_next(uint32_t *rng, uint32_t *echo_rng, noise_thunder_voice *voice,
                               float out[2]) {
  float t = (float)voice->elapsed;
  const noise_thunder_segment *segment = voice->segment;
  while (voice->next < voice->segments && segment[voice->next].start < t + 1.0f) {
    ++voice->next;
  }
  while (voice->first < voice->next &&
         t >= segment[voice->first].start + segment[voice->first].width) {
    ++voice->first;
  }
  /* Each segment adds a box over its arrival spread; the filters shape it into pulses. */
  float excitation[NOISE_THUNDER_BANDS][2] = {{0.0f}};
  for (unsigned i = voice->first; i < voice->next; ++i) {
    float x = t - segment[i].start;
    float overlap = fminf(x + 1.0f, segment[i].width) - fmaxf(x, 0.0f);
    if (overlap <= 0.0f) continue;
    float share = overlap + segment[i].roughness * random_gaussian(rng) * sqrtf(overlap);
    float amount[2] = {segment[i].gain[0] * share, segment[i].gain[1] * share};
    add_to_bands(excitation, segment[i].band, amount);
  }
  /* An echo spreads each box over its smear at the same total energy. */
  for (unsigned e = 0; e < NOISE_THUNDER_ECHOES; ++e) {
    noise_thunder_echo *echo = &voice->echo[e];
    float te = t - echo->delay;
    while (echo->next < voice->segments && segment[echo->next].start < te + 1.0f) {
      ++echo->next;
    }
    while (echo->first < echo->next &&
           te >= segment[echo->first].start + segment[echo->first].width + echo->smear) {
      ++echo->first;
    }
    for (unsigned i = echo->first; i < echo->next; ++i) {
      float width = segment[i].width + echo->smear;
      float x = te - segment[i].start;
      float overlap = fminf(x + 1.0f, width) - fmaxf(x, 0.0f);
      if (overlap <= 0.0f) continue;
      float share = segment[i].width / width *
          (overlap + segment[i].roughness * random_gaussian(echo_rng) * sqrtf(overlap));
      float level = hypotf(segment[i].gain[0], segment[i].gain[1]) * share;
      float amount[2] = {echo->gain[0] * level, echo->gain[1] * level};
      add_to_bands(excitation, echo_band(voice, segment[i].band, echo->range_log), amount);
    }
  }
  for (unsigned channel = 0; channel < 2; ++channel) {
    out[channel] = 0.0f;
    for (unsigned band = 0; band < NOISE_THUNDER_BANDS; ++band) {
      float pulse = biquad_next(&voice->pulse[band][channel], excitation[band][channel]);
      out[channel] += biquad_next(&voice->air[band][channel][1],
                                  biquad_next(&voice->air[band][channel][0], pulse));
    }
  }
  if (++voice->elapsed == voice->length) voice->length = 0;
}

/* Unity below 0.5, then a tanh knee toward 1: near booms have about 25 dB crest. */
static float thunder_limit(float x) {
  float magnitude = fabsf(x);
  if (magnitude <= 0.5f) return x;
  return copysignf(0.5f + 0.5f * tanhf(2.0f * (magnitude - 0.5f)), x);
}

/* Runs at a quarter rate: thunder is mostly below 2 kHz, and memory drops by four. */
static void thunder_reverb_next(noise_thunder_reverb *reverb, float send, float wet[2]) {
  reverb->input += send;
  if (++reverb->phase == THUNDER_REVERB_DECIMATION) {
    reverb->phase = 0;
    float *previous = reverb->output[0];
    float *current = reverb->output[1];
    previous[0] = current[0];
    previous[1] = current[1];
    /* Damping alpha 0.5 at 11025 Hz: about a 1.2 kHz loop low-pass. */
    noise_fdn_next(reverb->buffer, thunder_reverb_length, thunder_reverb_offset, &reverb->fdn, 0.5f,
                   reverb->input / THUNDER_REVERB_DECIMATION, current);
    reverb->input = 0.0f;
  }
  float blend = (float)reverb->phase / THUNDER_REVERB_DECIMATION;
  for (unsigned channel = 0; channel < 2; ++channel) {
    wet[channel] = reverb->output[0][channel] + blend *
        (reverb->output[1][channel] - reverb->output[0][channel]);
  }
}

void noise_thunder_next(noise_thunder *thunder, const noise_thunder_config *c,
                         noise_state *state, float *left, float *right) {
  float rate_per_min = state->weather.lightning_per_min;
  if (c->gain > 0.0f && rate_per_min > 0.0f) {
    int trigger = !thunder->started;
    thunder->started = 1;
    if (!trigger) {
      /* 32-bit comparison resolves rates far below 0.01 strikes/min. */
      float probability = rate_per_min / (60.0f * NOISE_SAMPLE_RATE_HZ);
      trigger = random_u32(&thunder->rng) < (uint32_t)(probability * 4294967296.0f);
    }
    if (trigger) {
      const position_polar *cell = &state->weather.cell;
      float x = cell->distance_m * sinf(cell->angle_rad) +
                c->scatter_m * random_gaussian(&thunder->rng);
      float y = cell->distance_m * cosf(cell->angle_rad) +
                c->scatter_m * random_gaussian(&thunder->rng);
      position_polar position = {fmaxf(200.0f, hypotf(x, y)), atan2f(x, y)};
      /* Capacity losses are recorded by noise_thunder_start for both paths. */
      if (position.distance_m <= NOISE_THUNDER_MAX_DISTANCE_M) (void)noise_thunder_start(thunder, state, position);
    }
  }
  float sum[2] = {0.0f, 0.0f};
  for (unsigned i = 0; i < NOISE_THUNDER_VOICES; ++i) {
    if (!thunder->voice[i].length) continue;
    float out[2];
    thunder_voice_next(&thunder->rng, &thunder->echo_rng, &thunder->voice[i], out);
    sum[0] += c->gain * out[0];
    sum[1] += c->gain * out[1];
  }
  if (c->reverb_gain > 0.0f) {
    float wet[2];
    /* Unity send: gain 0.5 puts the wet about 3 dB under the dry roll. */
    thunder_reverb_next(&thunder->reverb, sum[0] + sum[1], wet);
    sum[0] += c->reverb_gain * wet[0];
    sum[1] += c->reverb_gain * wet[1];
  }
  *left += thunder_limit(sum[0]);
  *right += thunder_limit(sum[1]);
}
