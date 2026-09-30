#include "noise_spatial.h"

#include <math.h>

#include "noise_internal.h"

int noise_listener_config_valid(const noise_listener_config *c) {
  return in_range(c->stereo_width_m, 0.0f, 0.5f) &&
         in_range(c->head_amount, 0.0f, 1.0f) &&
         in_range(c->rear_amount, 0.0f, 1.0f);
}

void noise_spatial_init(noise_spatial *voice, const noise_listener_config *listener,
                         position_polar position) {
  float radius = 0.5f * listener->stereo_width_m;
  float distance = position.distance_m;
  float lateral = sinf(position.angle_rad);
  float path[2];
  float k = NOISE_SAMPLE_RATE_HZ * radius / 343.0f;
  int head_enabled = radius > 0.0f && listener->head_amount > 0.0f;
  voice->head_feedback = head_enabled ? (k - 1.0f) / (k + 1.0f) : 0.0f;
  voice->reverb_gain = 1.0f / sqrtf(fmaxf(1.0f, distance));
  for (unsigned ear = 0; ear < 2; ++ear) {
    float cosine = ear == 0 ? -lateral : lateral;
    float gap = distance - radius;
    float straight = sqrtf(gap * gap + 2.0f * distance * radius * (1.0f - cosine));
    path[ear] = straight;
    voice->ear_gain[ear] = 0.707106781f / fmaxf(1.0f, straight);
    voice->head_b0[ear] = 1.0f;
    if (head_enabled) {
      float theta = acosf(cosine);
      float tangent_angle = acosf(radius / distance);
      if (theta > tangent_angle) {
        float around = sqrtf((distance - radius) * (distance + radius)) +
                       radius * (theta - tangent_angle);
        path[ear] += listener->head_amount * (around - straight);
      }
      /* Brown-Duda head shelf, bilinear transform, pole at 2c/a. */
      float alpha = 1.05f + 0.95f * cosf(theta * 1.2f);
      alpha = 1.0f + listener->head_amount * (alpha - 1.0f);
      voice->head_b0[ear] = (1.0f + alpha * k) / (1.0f + k);
      voice->head_b1[ear] = (1.0f - alpha * k) / (1.0f + k);
    }
  }
  float first_path = fminf(path[0], path[1]);
  for (unsigned ear = 0; ear < 2; ++ear) {
    float delay = (path[ear] - first_path) * (NOISE_SAMPLE_RATE_HZ / 343.0f);
    voice->ear_delay[ear] = (unsigned)delay;
    float f = delay - (float)voice->ear_delay[ear];
    /* Four-tap Lagrange delay adds one common frame of causal latency. */
    voice->delay_weight[ear][0] = -f * (f - 1.0f) * (f - 2.0f) / 6.0f;
    voice->delay_weight[ear][1] = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f;
    voice->delay_weight[ear][2] = -(f + 1.0f) * f * (f - 2.0f) / 2.0f;
    voice->delay_weight[ear][3] = (f + 1.0f) * f * (f - 1.0f) / 6.0f;
  }
  float rear = 0.5f * (1.0f - cosf(position.angle_rad));
  float cutoff = 18000.0f - 15000.0f * rear;
  voice->lowpass_alpha = -expm1f(-2.0f * NOISE_PI * cutoff / NOISE_SAMPLE_RATE_HZ);
}

float noise_spatial_next(noise_spatial *voice, const noise_listener_config *listener,
                         noise_bus *bus, float source) {
  voice->lowpass_state += voice->lowpass_alpha * (source - voice->lowpass_state);
  float direct = source + listener->rear_amount * (voice->lowpass_state - source);
  for (unsigned ear = 0; ear < 2; ++ear) {
    float filtered = voice->head_b0[ear] * direct +
        voice->head_b1[ear] * voice->head_previous_input +
        voice->head_feedback * voice->head_state[ear];
    voice->head_state[ear] = filtered;
    unsigned position = bus->position + voice->ear_delay[ear];
    for (unsigned tap = 0; tap < 4; ++tap) {
      bus->direct[ear][(position + tap) % NOISE_DIRECT_SAMPLES] +=
          filtered * voice->ear_gain[ear] * voice->delay_weight[ear][tap];
    }
  }
  voice->head_previous_input = direct;
  return voice->reverb_gain * source;
}

void noise_bus_next(noise_bus *bus, float *left, float *right) {
  *left = bus->direct[0][bus->position];
  *right = bus->direct[1][bus->position];
  bus->direct[0][bus->position] = 0.0f;
  bus->direct[1][bus->position] = 0.0f;
  if (++bus->position == NOISE_DIRECT_SAMPLES) bus->position = 0;
}

int noise_placement_valid(const noise_placement *p) {
  return in_range(p->stereo_width, 0.0f, 1.0f) &&
         in_range(p->min_distance_m, 0.25f, 100.0f) &&
         in_range(p->max_distance_m, p->min_distance_m, 100.0f);
}

position_polar noise_placement_position(const noise_placement *p, float distance_offset,
                                         float angle_offset) {
  position_polar position = {
    area_uniform_distance(p->min_distance_m, p->max_distance_m, distance_offset),
    NOISE_PI * p->stereo_width * angle_offset
  };
  return position;
}
