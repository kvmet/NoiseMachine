#ifndef NOISE_STORM_H
#define NOISE_STORM_H

#include "noise_types.h"

#define NOISE_STORM_CELLS 2u
#define NOISE_CONTROL_FRAMES 441u /* Weather updates 100 times a second. */

/* Weather held while noise_storm_config.manual is set. */
typedef struct noise_fixed_weather {
  float rain_mm_h;
  float wind_m_s; /* Mean; gusts still vary around it. */
  float wind_bearing_rad;
  float temperature_c;
  float lightning_per_min;
  position_polar cell; /* Where strikes land; distance 200..30000 m. */
} noise_fixed_weather;

/* How a storm cell's weather depends on its severity s, 0..1, and its position.
   Each *_min and *_max pair gives the value at s = 0 and s = 1. Distances are
   along the cell's track, positive ahead of the core, or across it. */
typedef struct noise_storm_shape {
  float peak_rain_min_mm_h; /* Peak rain is min × (max / min)^s. */
  float peak_rain_max_mm_h;
  float core_along_m; /* Gaussian radii of the heavy rain. */
  float core_across_m;
  float tail_share; /* Light rain behind the core, relative to the peak. */
  float tail_length_m;
  float tail_width_m;
  float front_min_m; /* Gust front distance ahead of the core; linear in s. */
  float front_max_m;
  float front_edge_m; /* Distance over which the front's wind and cooling rise. */
  float outflow_min_m_s; /* Outflow speed; linear in s. */
  float outflow_max_m_s;
  float outflow_decay_m; /* Behind the core. */
  float outflow_width_m;
  float cooling_min_c; /* Linear in s. */
  float cooling_max_c;
  float cooling_decay_m; /* Behind the core. */
  float cooling_width_m;
  float cooling_s; /* Storm-time constant while temperature falls. */
  float warming_s; /* Storm-time constant while temperature rises. */
  float lightning_min_per_min; /* Rate is min + (max - min) s². */
  float lightning_max_per_min;
  float build_share; /* Share of the track over which a cell grows. */
  float decay_share; /* Share of the track over which it dies. */
  float approach_m; /* A cell starts this far before its closest point and ends as far past. */
  float miss_m; /* Largest distance a track passes beside the listener. */
  float heading_spread_rad; /* Gaussian spread around the prevailing direction. */
} noise_storm_shape;

typedef struct noise_storm_config {
  int manual; /* 1: weather comes from fixed; 0: from the simulated storms. */
  noise_fixed_weather fixed;
  float time_scale; /* Storm seconds per real second; gusts and lightning stay real time. */
  float temperature_c; /* Air temperature away from storms. */
  float min_severity; /* Each storm draws its severity between these, 0..1. */
  float max_severity;
  float storms_per_hour; /* Mean arrival rate in storm time; zero keeps the sky clear. */
  float cell_speed_m_s;
  float breeze_m_s; /* Wind away from storms, blowing from where they come from. */
  float gust_intensity; /* Gust standard deviation over the mean wind, 0..1; both modes. */
  float gust_time_s; /* How long a gust lasts, 0.5..30 s; both modes. */
  noise_storm_shape shape;
} noise_storm_config;

typedef struct noise_storm_cell {
  unsigned active;
  float severity;
  float position[2]; /* Core relative to the listener, x right and y front, metres. */
  float heading[2]; /* Unit direction of travel. */
  float travelled_m;
} noise_storm_cell;

typedef struct noise_storm {
  uint32_t rng;
  uint32_t gust_rng;
  unsigned frame; /* Frames since the last weather update. */
  float prevailing[2]; /* Unit direction storms travel, fixed per seed. */
  float temperature_c;
  float gust; /* Current gust as a fraction of the mean wind. */
  noise_storm_cell cell[NOISE_STORM_CELLS];
} noise_storm;

#ifdef __cplusplus
extern "C" {
#endif

int noise_storm_config_valid(const noise_storm_config *c);
void noise_storm_config_default(noise_storm_config *c);
/* Starts inside a storm's rain unless storms_per_hour is zero. */
void noise_storm_init(noise_storm *storm, const noise_storm_config *c, noise_weather *weather,
                      uint32_t seed);
/* Keeps the storms; a changed climate temperature shifts the current temperature by the
   same amount. Recomputes weather without advancing time. */
void noise_storm_configure(noise_storm *storm, const noise_storm_config *previous,
                           const noise_storm_config *c, noise_weather *weather);
/* Advances one frame. Returns 1 when it updated weather. */
int noise_storm_next(noise_storm *storm, const noise_storm_config *c, noise_weather *weather);

#ifdef __cplusplus
}
#endif

#endif
