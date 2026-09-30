# Synthesis model

The shared engine is C11 with a C++ interface for Arduino. It generates signed
16-bit stereo PCM at 44,100 frames/s. The desktop renderer and sketch use the
same code. This document specifies the implemented model; it is not a claim
of calibrated acoustic pressure or a full fluid simulation.

## Signal path and units

Each played rain drop creates up to four damped modes from its surface: a
click, two resonances, and a bubble. Their sum feeds the direct stereo path
and a shared reverb. A noise bed stands in for drops too many to play one by
one; it joins the direct path only. Crickets and cicadas use the same direct path and
reverb. The direct path falls as 1/r past 1 m; the reverb send falls as 1/sqrt(r),
since open air has no room to hold a diffuse field. Thunder has its own reverb and limiter, then joins the direct mix.
Ambient layers join the stereo mix after the reverb.
`rain.gain` scales direct rain, the bed, and the reverb send. `master_gain` scales
the final output before conversion to PCM.

Every layer's gain 1 at its reference condition gives the same loudness,
-24 LUFS (BS.1770, power mean over eight seeds, default reverb, master gain 1),
so layer gains compare directly. The reference conditions are rain at 10 mm/h
on the default surfaces, wind at 10 m/s, crickets at 25 °C, cicadas at 30 °C,
and white noise, pink noise, and hum as they are. Thunder is louder on purpose:
the loudest 400 ms of a strike 2 km away reads -18 LUFS. Weather moves each
layer away from its reference; `tests/test_mix.c` checks each reference within
1 dB.

Distances and radii use metres; velocity uses m/s; angles use radians;
frequency uses Hz; damping uses reciprocal seconds. `radius_m` is the water
drop radius. `bubble_radius_m` is the radius of enclosed air, a separate
quantity. Atmospheric pressure is 101325 Pa. Water density is 1000 kg/m³,
and the air heat-capacity ratio is 1.4.

The original reference table gives atmospheric pressure in kPa. Minnaert's
formula below requires Pa when the other inputs use SI units.

## Ambient layers

`ambient_gain` contains an independent linear gain for white noise, pink
noise, 50 Hz hum, and 60 Hz hum. Wind, crickets, and cicadas have their own
`gain` in `wind`, `crickets`, and `cicadas`. Each can be zero or combined with
others. White noise, pink noise, and hum are identical in both
channels. The nature layers have independent stereo-width controls.

White noise maps the upper 24 bits of a xorshift32 stream to the interval
[-1, 1). Pink noise uses the existing Paul Kellet seven-state filter. Its
power density approximates 1/f, so equal frequency octaves have similar power. The coefficients are for 44.1 kHz;
changing the sample rate requires revisiting them. See [5].

The hum waveform is

    h(t) = [sin(2πft) + 0.30 sin(4πft) + 0.12 sin(6πft)] / 1.42

where f is 50 or 60 Hz. Harmonic weights are sound design choices. A table
contains one 882-sample period at 50 Hz. The 60 Hz oscillator interpolates
that table over its 735-sample period. An integer counter repeats after
4410 samples, the common period of both signals, without phase drift.

Wind starts with one common and two independent white-noise streams. Stereo
width crossfades each channel between the common and its independent stream.
A one-pole filter sets the brightness, and a second 120 Hz one-pole filter adds
low-frequency movement. Level, brightness, and balance follow the weather; see
Weather couplings. Wind uses a separate random stream, so enabling it does not change rain or the
other ambient layers.

Crickets are four persistent individuals. Each keeps its own pitch offset,
position, pulse timing, and three to five pulses per chirp. The call rate
follows temperature; see Weather couplings. Each chirps on a steady period of
1 / call rate, scaled by a fixed 0.9 to 1.1 per cricket, with 3 percent jitter per chirp; the small rate differences let the
chorus drift in and out of phase. Each cricket alternates singing and silent
bouts with exponential lengths, means 30 s and 10 s. Within each pulse the
carrier falls 3 percent, as the wing's tooth strikes slow. Pitch variation sets
the spread between crickets, up to plus or minus 30 percent of the configured
pitch. Each cricket is a point source through the same spatial model as a rain
drop: 1/r level, ear delay, head shadow, and rear filter. Distance is
area-uniform between the cricket distance bounds. Stereo width is angular
spread: at 0 every cricket is in front, at 1 they surround the listener. The
cricket signal before distance attenuation feeds the rain reverb, like the rain
send. Pitch changes apply at the next pulse; position and rate changes apply at
the next chirp.

Cicadas are four persistent individuals of one species over a distant chorus.
A cicada buckles its tymbals many times a second; each buckle is a click that
rings the abdomen. Each individual is a click train through a two-pole
band-pass at its body pitch: the configured pitch plus a fixed offset of up to
5 percent per cicada. Click intervals vary 1 percent at random. The click
rate control scales each species' own rate by 0.5 to 1.5.

Each species sings phrases of syllables and an optional held final note. Each
syllable's level rises and falls as a parabola while its pitch glides. A held
note swells in over up to 1 s, pulses at 2 to 4 Hz, then winds down over up
to 2 s while level falls to zero and pitch and click rate fall 15 percent.
Silence between calls is exponential. The values below are starting points
from descriptions of each song, not fitted to recordings.

| Species | Pitch | Clicks/s | Body Q | Syllables | Held note | Mean gap |
|---|---|---|---|---|---|---|
| Dog-day (US) | 5 kHz | 300 | 6 | none | 10 to 18 s, 40% pulsing | 20 s |
| Minminzemi (JP) | 5 kHz | 400 | 20 | 5 to 15 at 3/s, 70% sounding, rising 4% | 1 to 2 s, 20% pulsing | 8 s |
| Higurashi (JP) | 5 kHz | 500 | 30 | 20 to 40 slowing from 8/s to 6/s, 50% sounding, falling 5%, fading to 0.3 | none | 15 s |
| Aburazemi (JP) | 4.5 kHz | 450 | 4 | none | 5 to 20 s, 10% pulsing | 10 s |
| Niiniizemi (JP) | 7.5 kHz | 500 | 15 | none | 10 to 30 s, 5% pulsing | 10 s |
| Kumazemi (JP) | 5 kHz | 400 | 5 | 20 to 40 at 4/s, 60% sounding | none | 10 s |
| Pharaoh cicada (US) | 1.4 kHz | 300 | 10 | none | 1 to 3 s | 5 s |
| Scissor grinder (US) | 5.5 kHz | 300 | 6 | 50 to 100 at 5/s, 80% sounding | none | 20 s |
| Cigale grise (FR) | 4.5 kHz | 400 | 8 | 80 to 200 at 8/s, 40% sounding | none | 10 s |
| Green grocer (AU) | 4 kHz | 450 | 8 | none | 15 to 30 s, 15% pulsing | 15 s |

A low Q buzzes; a high Q rings across clicks and sounds tonal. The region is
where each species is typical. `noise_cicada_set_species` selects a species
and its typical pitch; the pitch control then adjusts it.

Each individual uses the same spatial model and reverb send as a cricket, with
its own distance bounds; stereo width is angular spread.

The distant chorus is uniform noise through the same band-pass at the
configured pitch, independent in each ear, with Q half the species' body Q
and at least 3. Its level follows a random target every 4 s with a 2 s time
constant. It bypasses the spatial model and the reverb. Crickets and cicadas use separate random
streams and do not allocate rain voices.

## Rain arrivals and size distribution

`weather.rain_mm_h` is a rain rate R in mm/h. Drop sizes follow the
Marshall-Palmer spectrum [9]:

    N(D) = 8000 exp(-ΛD) per m³ per mm,  Λ = 4.1 R^-0.21 per mm

Drops of diameter D reach each square metre of ground at N(D) v(D) per
second, where v is the terminal speed below. The engine sums this flux over
50 bins of 0.1 mm from 0.8 to 5.8 mm, the range of the terminal-speed fit.
The sum is the physical arrival rate per m². Each drop picks a bin by this
flux-weighted distribution, then a diameter uniform within the bin. Heavier
rain has a smaller Λ, so a larger share of its drops are large.

The physical rate is far above what the voices can play. Within 5 m,
0.5 mm/h gives about 12,000 drops/s and 150 mm/h about 700,000.
`rain.max_drops_per_s` is the budget of drops played one by one. The played
rate is the smaller of the budget and the physical rate over the annulus
between the distance bounds. Played drops land in the near ring from
`min_distance_m` to the radius that the budget fills at the physical density:

    near_m = sqrt(r_min² + played / (π × hits)),  at most r_max

where hits is the drops per m² per second on the surfaces, including walls
(see Driving rain).

When the budget covers the whole annulus, near_m is r_max and every drop
plays. Otherwise the rain bed stands in for the drops beyond near_m.

At each frame, one Bernoulli trial with probability played / 44100 decides
whether a drop arrives. This is a discrete approximation to a Poisson
process, not an exact continuous Poisson scheduler. In N frames, the count
has mean Np and variance Np(1-p). It allows at most one automatic arrival
per frame. At the maximum supported rate of 2000/s, count variance is
about 4.5% below the Poisson variance. The default budget is 900/s.

Arrivals are scheduled at the listener, following the arrival-time approach
in [2, section 3.1]. No falling particles, propagation warmup, or queued
flight trajectories are simulated.

Each arrival independently samples one of the first `rain.surface_count`
entries in `rain.surface` by its `coverage`, the surface's relative share of
the ground. Coverages need not sum to one. The default list has nine
surfaces with coverage Water 0.37, Dirt 0.21, Leaf 0.26, Concrete 0.15,
Glass 0.005, and Metal 0.005. Plastic, Asphalt, and Asphalt roof default to
zero. Coverage sets the share of drops on each surface; it does not place
surfaces around the listener.

### Driving rain

A surface with `vertical` set is a wall facing the wind. Falling rain misses
it; the wind carries drops into it. Each second, a square metre of wall
sweeps wind-speed cubic metres of air, so it takes U × C drops, where U is
the wind with gusts and C = Σ N(D) ΔD is the drops per cubic metre. A
square metre of ground takes the flux F = Σ N(D) v(D) ΔD. A wall's coverage
therefore weighs U × C / F against a horizontal surface's 1: zero in calm
air, and about 2.5 at 10 m/s in 10 mm/h rain. hits is F × (the sum of
weighted coverage) / (the sum of coverage), so walls add to the arrival
rate. Wall drops take their size from N(D) alone, with more small drops than
the ground, and strike at the wind speed on the assumption that drops move
with the air.

### Rain sheets

Gusts carry sheets of heavier and lighter rain across the listener. Every
10 weather updates (10 Hz), the rain records the gust fraction
g = wind / mean − 1 in a history of 512 entries (51 s). Air at upwind
distance a from the listener passed the ring's upwind edge (near_m − a) /
mean seconds ago; mean is the mean wind. The rain rate there is

    factor = max(0, 1 + rain.sheet_depth × g(then))

Air older than the history uses the oldest entry; calm air counts as
0.01 m/s. Candidate drops arrive at played × peak, where peak is the
largest factor across the played ring. Each candidate lands at a uniform
position and plays with probability factor / peak there. Sheets therefore
reach the windward ear first. Across a 2.2 m ring at 4 m/s, the right ear
leads the left by about 0.4 s with wind from the right. The mean played
rate stays near the budget, but a sheet's peak can exceed it. Depth zero
skips the extra draw, and gusts then leave the rain unchanged.

The azimuth is uniform over a circle. Radial distance is

    r = sqrt(r_min² + u(near_m² - r_min²)),  u uniform in [0, 1)

This makes positions uniform by area in the near ring, rather than
concentrating drops near the listener.

Rate, sizes, and near_m follow the weather 100 times a second. Sounding
drops keep their coefficients.

## Rain bed

The bed plays the drops beyond near_m as noise matched to the played rain.
It is a vocoder with 15 bands at half-octave spacing from 125 Hz to 16 kHz.
Each band is two cascaded band-pass biquads with Q 1.414; this spacing keeps
the summed response flat within about 2 dB. Analysis runs on each ear of
the played rain after spatial processing, so it carries distance gain, head
shadow, and the rear filter. Each ear's bed follows that ear's drops, so it
follows sheets that favor one side. Each band's power P_b is smoothed with a
0.5 s time constant. Synthesis passes independent uniform noise per band and
ear through the same filters, at gain

    g_b = sqrt(ratio × P_b / (σ² × U_b × plateau))

where σ² = 1/3 is the noise variance, U_b is the band's power gain for unit
white noise, and plateau is the bank's summed power gain averaged from
250 Hz to 8 kHz. The ratio is the power of the unplayed ring over the played
ring, for area-uniform drops at the ear gain 0.707 / max(1, r):

    ring(r) = r²/2 for r <= 1, 1/2 + ln r above
    ratio = (ring(r_max) - ring(near_m)) / (ring(near_m) - ring(r_min))

Gains update 100 times a second. Against every drop played one by one, the
bed holds the level within 1.5 dB and the spectral centroid within
10 percent. `rain.bed_gain` scales the bed after `rain.gain`; 1 matches the
drops it stands in for. The bed does not feed the reverb and uses its own
random stream.

## Drop velocity and excitation

The Dingle-Lee fit reproduced in [1, section 4.1.1] estimates terminal
speed from diameter d. In the following expressions, d is in millimetres
and the polynomial result is in centimetres per second:

    d <= 1.4: V_T = -17.8951 + 448.9498d + 16.3719d² - 45.9516d³
    d > 1.4:  V_T =  24.1660 + 448.8336d - 75.6265d² + 4.2695d³

Multiply by 0.01 to obtain m/s. For example, a 1 mm drop has a terminal
speed of about 4.015 m/s. Automatic drops land at terminal speed, or at
the wind speed on a vertical surface. Manual arrivals supply their own
velocity.

Mass is proportional to radius cubed, and kinetic energy is mV²/2.
The source amplitude follows the square root of relative kinetic energy:

    A = g_s × 0.004375 × (drop_radius / 0.0005)^(3/2) × V / 4

where g_s is the surface's `gain`, which scales its whole drop against the
other surfaces. This is a chosen conversion from impact energy to digital
amplitude, not a pressure calibration. It differs from the pressure
amplitude model in [1]. The layer's overall level follows the shared
reference under Signal path and units. The radius and speed in the
denominator define a 1 mm diameter reference drop at 4 m/s.

## Surfaces

A surface is a named parameter set, not a material type. `rain.surface`
holds up to nine `noise_surface` entries, of which the first
`rain.surface_count` are used. Every drop renders the same four damped
modes from its surface: a click, two resonances, and a bubble. Each min/max
pair is sampled uniformly per drop. Equal bounds use the value directly and
consume no random draw. `name` is a NUL-terminated label of up to 15 bytes
for hosts to show; the engine does not use it.

### Click

Every moving drop produces

    x_I(t) = A g_c exp(-βt) sin(2πft)

with click gain g_c sampled from `click_gain_min` to `click_gain_max`, f
sampled from `click_frequency_min_hz` to `click_frequency_max_hz`, and
β = `click_damping_ratio` × f. All default surfaces use f from 1 to 16 kHz and
β = 2f, following [1, section 4.1.1]. This produces a brief impact impulse.
The engine uses this temporal mode, not the paper's complete dipole field,
water-hammer pressure amplitude, or geometry-dependent radiation model.

### Resonances

Each of the two `mode` entries adds a damped sinusoid with its own
frequency, damping per second, and gain relative to A. A mode with gain
zero is not rendered. When either gain is above zero, each drop multiplies
both frequencies by one factor sampled from 1 - `detune` to 1 + `detune`.
The click gain does not scale the resonances.

A nonzero `lowpass_hz` passes the drop's click, resonances, and bubble
through two cascaded one-pole low-pass filters at that cutoff. The
Asphalt roof default uses this for sound transmitted through the roof and
ceiling to an indoor listener.

[3] demonstrates a finite-difference metal-bar model. This implementation
uses a small modal approximation to keep work bounded on ESP32, not that
bar solver. [1] uses recorded material textures; the engine does not use
those recordings or reproduce its VMD decomposition.

### Bubbles

Each automatic arrival on a surface creates a bubble with
`bubble_probability`. Bubble radius is sampled logarithmically from
`bubble_radius_min_m` to `bubble_radius_max_m`. The Water default uses
0.35 to 1.6 mm, which extends beyond the 0.16 to 0.47 mm range reported in
[1, section 4.1.2] as an explicit sound-design choice. A manually triggered
drop supplies any bubble radius from 0.16 to 4 mm, or zero for no bubble,
on any surface, and does not use the probability or radius range.

The resonance follows Minnaert's formula as presented in [1, equation 5]:

    f_B = sqrt(3γP / ρ_water) / (2πr_B)

A 0.4 mm bubble has a frequency of about 8208 Hz. Doubling its radius halves
its frequency. The smallest supported bubble remains below Nyquist.

The decay approximation from [4, section 3, equation 3] is

    β_B = 0.13/r_B + 0.0072/r_B^(3/2)

The bubble starts `bubble_delay_s` after the click; the defaults use 2 ms. Each
bubble samples a gain and a decay scale from their ranges. Gain multiplies
the click amplitude A g_c. The physical damping above is divided by the
decay scale; the Water default's 3 to 8 produces longer tails. Frequencies
remain fixed during a bubble's lifetime. The pitch-rise model described in
[3] and [4] is not implemented. If added, instantaneous frequency must be
integrated to get phase; substituting f(t)t directly doubles a linear
chirp's slope.

### Default surfaces

Water has click gain 0.15 to 0.5, no resonances, and bubbles with
probability 0.85, gain 1.2 to 2.5, and decay scale 3 to 8. The other
defaults have no bubbles, detune 0.15, and the settings below as
`(frequency Hz, damping per second)` for each mode. The second mode's gain
is half the listed resonance gain.

- Dirt: (450, 1200), (1100, 1800), resonance 0.35, click 1.0.
- Leaf: (1800, 800), (4200, 1400), resonance 0.50, click 1.0.
- Concrete: (1400, 1400), (3700, 2200), resonance 0.45, click 1.0.
- Glass: (3200, 160), (7100, 260), resonance 0.325, click 1.0.
- Metal: (1700, 90), (4300, 150), resonance 0.40, click 1.0.
- Plastic: (220, 110), (650, 220), resonance 0.65, click 0.50, low-pass 1600 Hz.
- Asphalt: (300, 1600), (900, 2600), resonance 0.25, click 0.30.
- Asphalt roof: (140, 300), (420, 700), resonance 0.40, click 0.25, low-pass 900 Hz.

The defaults represent different resonant responses, but are not measured
material constants or solutions for a particular object shape.

### Limits

| Field | Range |
| --- | --- |
| `surface_count` | 1 to 9 |
| `name` | NUL within 16 bytes |
| `coverage` | 0 to 1000; at least one used surface above zero |
| `gain` | 0 to 4; default 1 |
| `vertical` | 0 or 1 |
| `click_gain_min`, `click_gain_max` | 0 to 2 |
| `click_frequency_min_hz`, `click_frequency_max_hz` | 20 to 20000 Hz |
| `click_damping_ratio` | 0.05 to 50 |
| `mode[].frequency_hz` | 20 to 20000 Hz |
| `mode[].damping_per_s` | 1 to 20000 |
| `mode[].gain` | 0 to 4 |
| `detune` | 0 to 0.5 |
| `lowpass_hz` | 0, or 20 to 20000 Hz |
| `bubble_probability` | 0 to 1 |
| `bubble_radius_min_m`, `bubble_radius_max_m` | 0.16 to 4 mm |
| `bubble_gain_min`, `bubble_gain_max` | 0 to 8 |
| `bubble_decay_min`, `bubble_decay_max` | 0.25 to 20 |
| `bubble_delay_s` | 0 to 0.1 s |

Each max must be at least its min.

## Damped-mode renderer

Each mode precomputes its recurrence coefficients on arrival:

    q = exp(-β / sample_rate)
    ω = 2πf / sample_rate
    y[n+1] = 2q cos(ω)y[n] - q²y[n-1]
    y[0] = 0
    y[-1] = -A sin(ω)/q

This produces A exp(-βt) sin(2πft) using multiplies and adds per sample.
Modes retire after `ceil(ln(10000) × sample_rate / β)` frames, when the
analytic envelope is 80 dB below its starting amplitude. Voice retirement
uses lifetime, not instantaneous sample magnitude: a zero crossing must
not end a ringing mode.

Up to 128 drops can sound together. The active prefix of the voice array
is compacted when a voice ends. A new drop is rejected if the pool is
full; existing tails continue. `state.dropped_drops` reports these capacity
losses and manual triggers return `NOISE_VOICE_LIMIT`. No voice allocation
or file access occurs during rendering.

## Ear geometry and propagation delay

`listener.stereo_width_m` is the distance between ears or microphones. It defaults
to 0.18 m and accepts 0 to 0.5 m. `listener.head_amount` defaults to one; zero
disables the head filter and diffraction. `listener.rear_amount` independently
controls rear filtering. Both amounts accept 0 to 1.

Angle zero is front, π/2 is right, π is behind, and negative π/2 is left.
For source radius r and azimuth φ, the source position is
`(r sin φ, r cos φ)`. The ear positions are `(-a, 0)` and `(a, 0)`,
where a is half the width. Each ear receives its own distance gain:

    g_ear = (1/sqrt(2)) / max(1, distance_to_ear)

The 1 m floor caps near-field gain. The common factor keeps a centered
source's combined channel power near its mono power before head filtering.
There is no additional equal-power pan control. Direction comes from
ear geometry, arrival delay, and the head transfer function.

With the head disabled, path length is straight-line distance. With the
head enabled, a ray reaching the opposite side follows the tangent and
then the sphere's surface. Let θ be the angle between the ear's outward
axis and the source direction:

    θ_t = acos(a/r)
    L_straight = sqrt((r-a)² + 2ra(1-cos θ))
    L_head = L_straight                           if θ <= θ_t
    L_head = sqrt((r-a)(r+a)) + a(θ-θ_t)           otherwise
    L = L_straight + head_amount × (L_head-L_straight)

This finite-distance ray construction is the engine's geometric
approximation. It does not solve diffraction pressure around a sphere.
For distant side sources, it approaches the usual spherical-head path
difference `a(1+π/2)`. At the default width, that is about 675 microseconds;
the corresponding spaced-microphone delay is about 525 microseconds.

Only the difference between the ear paths is delayed:

    delay_ear = (L_ear - min(L_left, L_right)) × 44100 / 343

This preserves the interaural delay while scheduling the first arrival
directly. Absolute flight time from the impact is not added again.

A four-tap cubic Lagrange interpolator implements fractional frames.
For fractional delay f, its tap weights at offsets 0, 1, 2, 3 are:

    [-f(f-1)(f-2)/6, (f+1)(f-1)(f-2)/2,
     -(f+1)f(f-2)/2, (f+1)f(f-1)/6]

The mean delay is 1+f, adding one common frame to both channels.
Interpolation has frequency-response error near Nyquist; it is not an
ideal all-pass delay. Each voice adds its contributions into two shared
128-frame circular buffers. This avoids a separate delay buffer per
drop. The supported geometry needs at most 101 whole frames of relative
delay, leaving room for all four taps. Queued samples survive voice
retirement.

## Analytic head transfer function

The head-shadow filter follows Brown and Duda [6, equations 3 to 5]:

    α(θ) = 1.05 + 0.95 cos(1.2θ)
    H(s) = (αs + 2c/a) / (s + 2c/a)

Its DC gain is one. At high frequency, the near ear can receive a gain
of two, while the far ear is attenuated. This supplies frequency-dependent
level and phase cues in addition to the path delay.

The implementation blends α toward one by `head_amount`, then applies a
bilinear transform. With `k = sample_rate × a/c`:

    b0 = (1 + αk)/(1+k)
    b1 = (1 - αk)/(1+k)
    feedback = (k-1)/(k+1)
    y[n] = b0 x[n] + b1 x[n-1] + feedback y[n-1]

Width zero bypasses the head filter and removes interaural delay. Head
amount zero is an exact filter bypass. Each voice retains 256 frames
after its modes end so the direct filters can settle.

This is an analytic horizontal HRTF component, not measured HRIR
convolution. Pinna notches, elevation, torso reflections, and head
tracking are absent. Front and rear have the same spherical-head
response, so an independently tunable one-pole filter supplies a broad
rear cue:

    rear = (1-cos φ)/2
    cutoff = 18000 - 15000 × rear
    α_lp = 1-exp(-2π × cutoff/44100)
    low[n] = low[n-1] + α_lp × (source[n]-low[n-1])
    direct[n] = source[n] + rear_amount × (low[n]-source[n])

The cutoff ranges from 18 kHz in front to 3 kHz directly behind.
This rear filter is a sound design choice; it does not guarantee
front/back discrimination. Headphones preserve the separate ear signals.
Speaker playback introduces acoustic crossfeed; no crosstalk cancellation
is implemented. Width zero makes direct rain mono, but the shared reverb
can still be stereo. Set reverb gain to zero for a strictly mono result.

## Shared reverb

Each source sends the same fraction of its unattenuated signal to one
shared reverb, before rear filtering and distance gain. Larger or faster
drops still send more because their source amplitudes are greater.
`reverb_gain` controls the return, with a default of 0.12.

The reverb is a six-line feedback delay network. Its line lengths are 739,
953, 1151, 1327, 1471, and 1663 samples. A normalized symmetric conference
matrix mixes every line into the other five without direct self-feedback.
The matrix is orthogonal, so the scattering stage preserves energy.

For each line, its delayed value passes through a one-pole low-pass with
alpha 0.16. Each line then applies

    feedback_i = 0.001^(line_length_i / (0.65 × sample_rate))

All feedback gains are below one and the low-pass has no gain above one, so
the network decays without input. The 0.65 s parameter is a nominal
low-frequency decay target; high frequencies decay faster. Six staggered
delays and full cross-line scattering create a denser tail than the previous
four-line network. Left and right returns use orthogonal circular projections
of the delayed signals.

Constant send with falling direct gain makes distant drops relatively
wetter. This is a useful diffuse-field approximation, not an outdoor
propagation solver or a room model. There are no reflecting walls,
material absorption measurements, or air-absorption model. With reverb
gain zero, the network is bypassed.

## Thunder

Thunder is a triggered event, not a continuous layer. The model follows
Ribner and Roy [7]: a tortuous lightning channel is a chain of short
segments, and each segment emits an N-wave. A segment seen side-on delivers
its whole length at once and gives a sharp boom. A segment seen end-on
spreads its energy over its arrival spread and gives a roll. The sum over
the channel, in arrival order, is the thunder signature.

Each strike builds a new channel with the listener at the origin, x right,
y front, and z up. The channel base sits on the ground at the strike
distance and azimuth.

- Main channel: height 1.5 to 4 km; path length 1.15 times the height.
- Ground branches: 1 to 3. Each leaves the main channel at 20% to 90% of
  its path, runs 200 to 1200 m, and leans down and outward at weight 0.4.
- In-cloud arms: 2 or 3, each 1.5 to 5 km, all leaving the channel top.
  Arm headings are evenly spaced with a random offset of up to a quarter
  of the spacing, so some arm usually arrives after the channel top. Arms
  add incoherently, so each has weight 0.6/sqrt(arms).

Every part is a random walk. Each step adds a Gaussian direction change
and a pull toward its preferred direction: up for the main channel, level
for arms, and down and outward for branches. This gives a mean direction
change of 16 degrees, as Hill measured [8], and a mean lean of 28 degrees.
Step length is uniform in 0.5 to 1.5 times a mean that fits the whole
channel into 256 segments with 5% spare.

A part's sound stops at any end where its arrivals are still getting
later. If parts stop there at full strength, the roll ends with a cut, and
the cut lands in one ear before the other. Arms that head back toward the
listener pass nearly overhead, so their arrivals pile up just before the
channel top's and stop with it. Each part therefore fades to zero over 40%
of its segments toward every such end: the main channel toward its top,
and arms and branches toward either end. An arm heading away keeps full
strength at the top, so it takes over from the parts that stop there.

Each segment records its first arrival time, its arrival spread, and a gain
per channel. The spread combines the range difference between its ends
with a random-walk wander of 0.28 sqrt(3 × length) m, so even a side-on
segment spreads a little. Amplitude is proportional to weight × length /
range, divided by the spread, so every segment delivers energy in
proportion to its length. A constant-power pan places each segment by its
own azimuth, so the roll moves across the stereo field.

Real channels are rough below any segment length. The renderer models
this fine structure as one random pulse per 3 m of channel. Over one frame
their sum adds Gaussian noise to each segment's box. Its variance, relative
to the box, is 0.09 divided by the fine pulses per frame, so a segment with
a short spread is rougher. This keeps the roll grainy instead of smooth.

Later arrivals travel farther, so the tail is darker than the onset and its
N-waves are longer. Each voice has three direct range bands, evenly spaced
in log range. Band 0 uses the strike distance d; band 2 uses d times the
ratio of the farthest to the nearest segment range. Each segment splits its
gain linearly between the two bands nearest its range. A fourth band holds
echo paths longer than the direct span. Each band's excitation passes
through two filters per channel:

- Pulse: a band-pass at 1/period with Q 0.7 shapes the boxes into N-waves.
  The period is 6 to 14 ms at 1 km, drawn once per strike, and lengthens
  with the fourth root of band distance.
- Air: a fourth-order Butterworth low-pass. Its cutoff is
  1000 × (1000/r)^0.6 Hz for band distance r, limited to 150 to 6000 Hz.

At 700 m this lowers the spectral centroid of the roll from about 100 Hz
at onset to about 70 Hz 3 to 8 s later.

Six ground reflectors, placed once per seed, give every strike the same
terrain. Each sits 300 m to 2.5 km away at uniform azimuth, with
reflectivity 0.3 to 0.6 and a roughness smear of 0.1 to 0.4 s. A reflector
returns the whole strike: each segment arrives again, delayed by the extra
path from the channel's centroid to the reflector to the listener. The echo
comes from the reflector's azimuth, at reflectivity × direct / echo path,
spread over the segment's spread plus the smear at the same energy. Echo
ranges map onto the bands, so echoes are darker than the direct roll.
The extra path is at most twice the reflector distance, so an echo trails
by at most 14.6 s, and its gain is at most 0.6. Echo fine-structure noise uses its own random stream. Rendering the
echoes costs about six times the direct segment work.

A voice retires 4096 frames after its last arrival, so the filters ring
out to exact silence. A limiter has unity gain below 0.5 of full scale and
a tanh knee toward 1 above it. Near booms keep about 25 dB of crest factor.

The thunder reverb is a separate six-line network like the shared reverb.
It runs at a quarter of the sample rate, since thunder is mostly below
2 kHz, and interpolates its output back up. Line lengths are 557, 719, 887,
1063, 1297, and 1609 samples at 11025 Hz: 50 to 146 ms, an echo spacing
like terrain. The loop low-pass has alpha 0.5, about 1.2 kHz.
`thunder.reverb_decay_s` sets the time to fall 60 dB. The send is the
dry sum at unity; gain 0.5 puts the wet about 3 dB under the dry roll.

Automatic strikes follow `weather.lightning_per_min`. The first frame with a
nonzero rate strikes once, then a Bernoulli trial per frame runs at the rate
/ 60 per second. The comparison uses 32 bits, so slow rates keep their
resolution. Strikes scatter around the storm core: Gaussian with
`thunder.scatter_m` (4 km) per axis, at least 200 m away. Strikes beyond 15 km are skipped, since thunder is
rarely heard that far. Two voices can overlap. A strike
that finds both busy is rejected and counted in `state.dropped_thunder`.

## Storm simulation

`state.weather` holds what every sound module reads: rain rate, mean wind
and wind with gusts, the bearing the wind comes from, temperature, lightning
rate, and the position of the nearest storm core. The storm module updates
it every 441 frames, 100 times a second. A frame counter sets the update
times, so they do not depend on fill size.

With `storm.manual` set, the default, weather comes from `storm.fixed`.
Gusts still vary around the fixed mean wind. Otherwise the engine simulates
passing storms.

`storm.shape` sets every distance, rate, and amount below; the numbers given
are its defaults. Each quantity that grows with severity s has a mild value
at s = 0 and a severe value at s = 1.

Storms arrive in storm time as a Poisson process at `storms_per_hour`, one
trial per update, while fewer than two are active. A new cell starts
`approach_m` (40 km) before its closest point to the listener, travels twice
that at `cell_speed_m_s`, and ends. Tracks follow a prevailing direction
drawn once per seed, with a Gaussian heading spread of 0.35 rad, and pass
the listener at a miss distance uniform within `miss_m` (10 km) either side. Severity is uniform between
`min_severity` and `max_severity`. When `storms_per_hour` is above zero, the
first storm starts between 1 km before and 4 km past its closest point,
within 2 km of the listener, so a new engine starts in rain.

A cell's stage rises over the first `build_share` (30 percent) of its track
and falls over the last `decay_share` (35 percent), each a smoothstep; the
two shares sum to at most 1. At the listener, with the core a
distance along ahead on its track and a distance across beside it:

- Rain: stage × peak × (core + tail), where the peak is
  mild × (severe / mild)^s: 2 mm/h at severity 0, 150 mm/h at 1. The core is Gaussian with σ 3 km along and 10 km across, a
  squall line wider than it is deep. Behind the core a tail adds 10 percent
  of the peak, decaying over 15 km, with σ 15 km across.
- Wind: outflow blowing away from the core at stage × (4 + 20 × s) m/s. It
  reaches (4 + 4 × s) km ahead of the core with a 1.5 km edge, the gust front, and decays over 6 km behind; σ 12 km across. It adds
  to the breeze, which blows along the prevailing direction.
- Cooling: stage × (3 + 7 × s) °C, with the same front, decaying over 25 km
  behind; σ 15 km across.
- Lightning: stage² × (0.5 + 11.5 × s²) flashes/min.

Rain adds over cells, limited to 200 mm/h, and wind vectors add. Lightning
and the reported position come from the nearest cell. Temperature relaxes
toward `storm.temperature_c` minus the largest cooling, with a 240 s time
constant while falling and 2400 s while rising.

The gust g is an Ornstein-Uhlenbeck process with time constant
`storm.gust_time_s` (4 s) and standard deviation `storm.gust_intensity`
(0.3), in both weather modes. Wind with gusts is max(0, mean × (1 + g)), so
gusts scale with the current mean wind. Gusts use their own random stream.

`time_scale` multiplies storm time: cell motion, storm arrivals, and
temperature relaxation. Gusts, lightning timing, drops, and insect calls
stay real time. The shapes and constants give a plausible sequence, a gust
front and cooling several minutes before the heaviest rain and a light tail
after it; they are not fitted to measurements.

## Weather couplings

Each module reads the weather at every update.

- Rain: arrival rate, drop sizes, near ring, bed, wall share, and sheets, as
  above.
- Wind: level is `wind.gain` × min(speed, 35) / 10, so the gain is the
  level at 10 m/s and level rises 6 dB per doubling of speed. This is a
  fitted curve, not a flow model. Brightness b = min(1, speed / 30) sets the
  air cutoff to `wind.brightness` × 400 × 20^b Hz, at most 18 kHz.
  `wind.rumble` scales the 120 Hz rumble. The ear facing the wind gets
  sqrt(1 + `wind.balance`) times the level and the other ear
  sqrt(1 - `wind.balance`); the default balance is 0.5. Levels glide with a
  20 ms time constant.
- Crickets: Dolbear's law [10] gives 7.2 T - 32 chirps/min at T °C. The
  call rate is that times `call_rate_scale`, limited to 0.05 to 10 chirps/s.
  Crickets stay silent below `min_temperature_c` (13 °C), in rain above
  `max_rain_mm_h` (0.5), or in mean wind above `max_wind_m_s` (8 m/s). A
  chirp in progress finishes.
- Cicadas: silent below `min_temperature_c` (22 °C) or in rain above
  `max_rain_mm_h` (0.5). A call that comes
  due while silent waits another rest. The chorus level follows with a 5 s
  time constant.
- Thunder: rate and position, as above.

The default insect thresholds are chosen values, not measured behavior.

## API and limits

Initialize a `noise_config` with `noise_config_default`, then edit values
before calling `noise_init`. Configuration is copied into the engine.
A successful initialization resets all voices, random streams, filters,
and delay lines. Invalid configuration returns `NOISE_INVALID_CONFIG`
and leaves the engine unchanged. Do not render an uninitialized engine.
`noise_config_valid` applies the same check without an engine, so a
control thread can reject a configuration before handing it to audio.

`noise_fill` takes a count of stereo frames, writes twice that many
interleaved int16 samples, and returns the frame count. A zero-frame call
changes no state and may use a null output pointer. Other calls require
valid storage. Do not mutate engine fields. The public `state` is for
inspection by the owning thread.

`noise_set_config` applies a new configuration while playing. Voices,
filters, and random streams continue. It validates like `noise_init` and
leaves the engine unchanged on `NOISE_INVALID_CONFIG`. Storms in progress
continue. A changed `storm.temperature_c` shifts the current temperature by
the same amount. Weather is recomputed at once without advancing time, so
reapplying the same configuration changes nothing. Distance, head, width, and surface changes apply
to new drops and insect calls; sounding voices keep their spatial settings.
Call it between fills on the audio thread.

`noise_get_status` reports the weather and what it does to each layer: rain
arrivals and played drops per second, the bed's share of rain power, the
wind level relative to its gain, the cricket chirp rate and how many
crickets are in a singing bout, cicada chorus activity, and for each insect
layer the `NOISE_QUIET_*` flags naming the conditions that silence it. Call
it on the audio thread; hosts copy the result to their control thread.

`noise_trigger_thunder` accepts a strike distance from 200 m to 15 km and an
angle from negative 2π to positive 2π. Invalid strikes return
`NOISE_INVALID_STRIKE` and leave the engine unchanged. A strike with both
voices busy returns `NOISE_VOICE_LIMIT`.

`noise_trigger_drop` accepts a physical drop at the listener's arrival
time on surface index `surface`. It validates the index against
`rain.surface_count`, radius
(0.4 to 2.9 mm), velocity (0 to 40 m/s), distance (0.25 to 100 m), angle
(negative 2π to positive 2π), and bubble radius (zero, or 0.16 to 4 mm).
Zero velocity succeeds without allocating a voice. Invalid drops leave
the engine unchanged. Call it between fills from the same audio thread.
Do not call it concurrently with rendering.

Configuration ranges are:

- Ambient, master, and reverb gains: 0 to 1 each. Rain gain: 0 to 4, so rain on
  quiet surfaces can reach the other layers.
- Wind gain, the level at 10 m/s, stereo width, and balance: 0 to 1;
  balance default 0.5. Brightness: 0.25 to 4, default 1. Rumble: 0 to 2,
  default 1.
- Insect thresholds: minimum temperature -10 to 45 °C, maximum rain 0 to
  200 mm/h, cricket maximum wind 0 to 40 m/s.
- Cricket call rate scale: 0.1 to 2, default 0.5. Pitch: 2 to 8 kHz.
  Pitch variation and stereo width: 0 to 1. Distance bounds: 0.25 to 100 m,
  ordered; defaults 2 and 15 m.
- Cicada species: any in the table above. Pitch: 1 to 10 kHz, default 5 kHz. Click rate scale: 0.5 to 1.5. Chorus and stereo width: 0 to 1. Distance bounds: 0.25 to 100 m, ordered;
  defaults 5 and 30 m.
- Thunder gain: 0 to 1, default 0. Reverb gain: 0 to 1, default 0.5.
  Reverb decay: 0.5 to 10 s, default 3.5 s. Scatter: 0 to 10 km, default 4.
- `storm.manual`: 0 or 1, default 1.
- Fixed weather: rain 0 to 200 mm/h, default 0; wind 0 to 40 m/s, default 3;
  wind bearing and storm angle -2π to 2π, default 0; temperature -10 to
  45 °C, default 25; lightning 0 to 30 flashes/min, default 0; storm
  distance 200 m to 30 km, default 5 km.
- Storms: time scale 1 to 600, default 1; clear-sky temperature -10 to
  45 °C, default 25; severity bounds 0 to 1, ordered, defaults 0.2 and 0.8;
  0 to 4 storms per hour, default 0.5; cell speed 3 to 30 m/s, default 10;
  breeze 0 to 15 m/s, default 2; gust intensity 0 to 1, default 0.3; gust
  time 0.5 to 30 s, default 4.
- Storm shape: each mild and severe pair ordered. Peak rain 0.1 to 200 mm/h.
  Core depth 0.5 to 20 km, core width 0.5 to 50 km. Tail share 0 to 1; tail
  length and width 1 to 50 km. Front lead 0 to 20 km; front edge 0.1 to
  10 km. Outflow 0 to 40 m/s; its decay 0.5 to 50 km and width 1 to 50 km.
  Cooling 0 to 20 °C; its decay 1 to 100 km and width 1 to 50 km. Cooling
  time 10 to 3600 s; warming time 60 to 36000 s. Lightning 0 to 30
  flashes/min. Build share 0.05 to 0.9 and decay share 0.05 to 0.95,
  summing to at most 1. Approach 10 to 100 km. Miss 0 to 30 km. Heading
  spread 0 to π.
- Played drop budget: 0 to 2000/s, default 900. Bed gain: 0 to 4, default 1.
  Sheet depth: 0 to 2, default 1.
- Rain distance bounds: 0.25 to 100 m, ordered; equal bounds make a ring.
- Stereo width: 0 to 0.5 m; head and rear amounts: 0 to 1.
- Surface fields: see the table under Surfaces.

Separate random streams drive ambient samples, wind, crickets, cicadas,
thunder, thunder echoes, echo terrain, arrivals, drop properties, the rain
bed, storms, and gusts. Enabling ambient sound or
thunder cannot change the rain sequence. Seed zero aliases seed one. Results repeat for the same build,
configuration, and seed, independent of fill size. Floating-point and
libm differences can prevent bit-identical output across CPU/toolchain
combinations. The generator is for sound, not cryptography.

The output converter saturates only when the final mix exceeds the PCM
range and increments `state.clipped_samples` for each affected channel
sample. There is no automatic gain control. Reduce gains if the counter
is nonzero. The host reports clipping, generated drops, capacity losses,
and peak voice count.

## ESP32 and validation

The sample rate, channel count, and pool size are compile-time constants.
The engine occupies 108,448 bytes with the tested host ABI, plus 1,024
bytes for a 256-frame PCM buffer. Confirm `sizeof(noise_gen)` on the
target ABI. Keep the generator in static storage, not a small task stack. Buffers are
caller-owned. Trigonometry, exponentials, and square roots for drops run
on arrival; all per-mode audio work is a recurrence. The hum table is
built once at initialization. Active drops are scanned every frame, so
dense rain on ringing surfaces costs more than noise alone.

The sketch demonstrates engine integration but does not configure an
I2S peripheral, DAC, amplifier, or pins. Its loop must be paced by a real
audio sink. Feed a blocking I2S writer or a buffered audio task; do not
perform synthesis, logging, or initialization in an interrupt handler.
At 256 frames per buffer, the deadline is about 5.80 ms. A host CPU
benchmark does not establish that an ESP32 meets that deadline.

Run `python3 tests/check.py` from the repository root. It builds into a
temporary directory using the system C/C++ compilers, without adding
project dependencies. Checks cover analytic bubble frequency and decay,
hum fundamentals and harmonics, spectral slopes, rain arrivals against the
Marshall-Palmer flux, drop sizes against its distribution, the bed against
played rain, driving rain rate, sizes, and impact speed, sheets that follow
the wind bearing, the bed per ear, a storm passage, time scale, gusts, insect and thunder
couplings, voice exhaustion and recycling, silence, block-size
independence, malformed CLI values, WAV headers, all nine surfaces at
150 mm/h without clipping and with under 1% of drops lost to the voice pool, and
linking the sketch against the C engine. Spatial checks cover rendered
phase, ear symmetry, head shelf gain, width bypass, distance gain,
reverb-send independence, rear filtering, and maximum delay bounds.
Thunder checks cover validation, voice limits, retirement to exact silence,
low-frequency dominance, distance filtering, panning, reverb decay, tapered
endings over 12 strikes, a tail darker than its onset, echoes after the
direct arrivals, automatic rate, and rain-stream independence.
A host stub checks linkage only;
it does not emulate ESP32 peripherals or prove the Arduino SDK build.

## References

Local reference copies in `refs/` are excluded from version control.
Use the publication links below; the supplied mini-project is cited as
an unpublished source.

1. Shiguang Liu, Haonan Cheng, and Yiying Tong. *Physically-based Statistical
   Simulation of Rain Sound*. ACM Transactions on Graphics 38(4), article
   123, 2019.
   [DOI](https://doi.org/10.1145/3306346.3323045).
   Sections 4.1 and 4.2 and tables 1 and 2 supply the impact model,
   bubble resonance, diameter bins, and physical constants. Its sound
   banks, material texture extraction, and propagation solver are not used.
2. Stanley J. Miklavcic, Andreas Zita, and Per Arvidsson.
   *Computational Real-Time Sound Synthesis of Rain*. DAFx 2004,
   pp. 169 to 172. [Conference paper](https://www.dafx.de/paper-archive/2004/P_169.PDF).
   Section 3.1 motivates superposition and scheduling arrival times.
   The engine does not evaluate its boundary integral or cone model.
3. Emil Sønderskov Hansen. *Rain Drops on a Uniform Metal Bar*.
   Sound Processing mini-project, Aalborg University, undated.
   Unpublished source supplied by the project author; not redistributed.
   Pages 1 to 3 discuss bubbles and finite-difference bar synthesis.
   This is a secondary teaching reference; the bubble decay equation
   was checked against [4].
4. Kees van den Doel. *Physically Based Models for Liquid Sounds*.
   ACM Transactions on Applied Perception 2(4), pp. 534 to 546, 2005.
   [Author's manuscript](https://persianney.com/kvdoelcsubc/publications/tap05.pdf),
   [DOI](https://doi.org/10.1145/1101530.1101554).
   Section 3 supplies the bubble damping approximation.
5. Paul Kellet's filter and discussion in Robin Whittle's
   [DSP Generation of Pink Noise](https://www.firstpr.com.au/dsp/pink-noise/),
   October 1999. Source for the pink filter coefficients and their
   intended sample rate.
6. C. Phillip Brown and Richard O. Duda. *A Structural Model for Binaural
   Sound Synthesis*. IEEE Transactions on Speech and Audio Processing
   6(5), pp. 476 to 488, 1998.
   [Paper](https://users.umiacs.umd.edu/~ramanid/cmsc828d_audio/BrownDuda.pdf),
   [DOI](https://doi.org/10.1109/89.709673).
   Section II.B supplies the spherical-head filter. The other structural
   components from the paper are not implemented.
7. H. S. Ribner and D. Roy. *Acoustics of Thunder: A Quasilinear Model for
   Tortuous Lightning*. Journal of the Acoustical Society of America 72(6),
   pp. 1911 to 1925, 1982. [DOI](https://doi.org/10.1121/1.388621).
   Supplies the segment-sum model: each segment emits an N-wave, and its
   orientation to the listener sets boom or roll.
8. R. D. Hill. *Analysis of Irregular Paths of Lightning Channels*. Journal
   of Geophysical Research 73(6), pp. 1897 to 1906, 1968.
   [DOI](https://doi.org/10.1029/JB073i006p01897).
   Supplies the 16 degree mean direction change between channel segments.
9. J. S. Marshall and W. McK. Palmer. *The Distribution of Raindrops with
   Size*. Journal of Meteorology 5(4), pp. 165 to 166, 1948.
   [DOI](https://doi.org/10.1175/1520-0469(1948)005<0165:TDORWS>2.0.CO;2).
   Supplies the exponential drop size spectrum and its dependence on rain
   rate.
10. A. E. Dolbear. *The Cricket as a Thermometer*. The American Naturalist
    31(371), pp. 970 to 971, 1897.
    [DOI](https://doi.org/10.1086/276739).
    Supplies the chirp rate against temperature.
