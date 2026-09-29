# Synthesis model

The shared engine is C11 with a C++ interface for Arduino. It generates signed
16-bit stereo PCM at 44,100 frames/s. The desktop renderer and sketch use the
same code. This document specifies the implemented model; it is not a claim
of calibrated acoustic pressure or a full fluid simulation.

## Signal path and units

Each rain arrival creates up to three damped modes: an impact and either a
water bubble or two material modes. Their sum feeds the direct stereo path
and a shared reverb. Thunder joins the direct mix and sends its clap to the
same reverb. Ambient layers join the stereo mix after the reverb.
`rain_gain` scales both direct rain and the reverb send. `master_gain` scales
the final output before conversion to PCM.

Distances and radii use metres; velocity uses m/s; angles use radians;
frequency uses Hz; damping uses reciprocal seconds. `radius_m` is the water
drop radius. `bubble_radius_m` is the radius of enclosed air, a separate
quantity. Atmospheric pressure is 101325 Pa. Water density is 1000 kg/m³,
the air heat-capacity ratio is 1.4, and gravity is 9.81 m/s².

The original reference table gives atmospheric pressure in kPa. Minnaert's
formula below requires Pa when the other inputs use SI units.

## Ambient layers

`ambient_gain` contains an independent linear gain for white noise, pink
noise, 50 Hz hum, 60 Hz hum, wind, crickets, and cicadas. Each can be zero or
combined with others. White noise, pink noise, and hum are identical in both
channels. The nature layers have independent stereo-width controls.

White noise maps the upper 24 bits of a xorshift32 stream to the interval
[-1, 1). Pink noise uses the existing Paul Kellet seven-state filter, with
its output scaled by 0.11. Its power density approximates 1/f, so equal
frequency octaves have similar power. The coefficients are for 44.1 kHz;
changing the sample rate requires revisiting them. See [5].

The hum waveform is

    h(t) = [sin(2πft) + 0.30 sin(4πft) + 0.12 sin(6πft)] / 1.42

where f is 50 or 60 Hz. Harmonic weights are sound design choices. A table
contains one 882-sample period at 50 Hz. The 60 Hz oscillator interpolates
that table over its 735-sample period. An integer counter repeats after
4410 samples, the common period of both signals, without phase drift.

Wind starts with one common and two independent white-noise streams. Stereo
width crossfades each channel between the common and its independent stream.
A one-pole filter sets brightness from a 400 Hz cutoff at zero to 8 kHz at
one. A second 120 Hz one-pole filter adds low-frequency movement. Gusts move
between random amplitude targets. Gust rate sets both the target interval and
the smoothing rate; gust depth blends between constant and modulated amplitude.
Wind uses a separate random stream, so enabling it does not change rain or the
other ambient layers.

Crickets are four persistent individuals. Each keeps its own pitch offset,
position, pulse timing, and three to five pulses per chirp. Each
chirps on a steady period of 1 / call rate, scaled by a fixed 0.9 to 1.1 per
cricket, with 3 percent jitter per chirp; the small rate differences let the
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

Cicadas use three slightly detuned carrier oscillators. A fourth oscillator
amplitude-modulates the chorus at the configured pulse rate. Texture blends in
high-pass noise. Stereo width crossfades between the common carrier and two
detuned side carriers, and between common and independent noise. Crickets and
cicadas use separate random streams and do not allocate rain voices.

## Rain arrivals and size distribution

Rain intensity I is a dimensionless control in [0, 1], not mm/hour.
The requested event rate is

    λ = I × max_drops_per_s

At each frame, one Bernoulli trial with probability λ / 44100 decides
whether a drop arrives. This is a discrete approximation to a Poisson
process, not an exact continuous Poisson scheduler. In N frames, the count
has mean Np and variance Np(1-p). It allows at most one automatic arrival
per frame. At the maximum supported rate of 2000/s, count variance is
about 4.5% below the Poisson variance. The default maximum is 900/s.

Arrivals are scheduled at the listener, following the arrival-time approach
in [2, section 3.1]. No falling particles, propagation warmup, or queued
flight trajectories are simulated.

Diameter distributions use [1, table 1]:

- Light: 84% between 0.8 and 1.1 mm, 16% between 1.1 and 2.2 mm.
- Heavy: 32% between 0.8 and 1.1 mm, 61% between 1.1 and 2.2 mm,
  and 7% above 2.2 mm.
- Very heavy: 24%, 52%, and 24% in those same three bins.

The engine assigns these distributions to intensity 0, 0.5, and 1,
respectively, and interpolates probabilities between them. At intensity
zero, there are no events even though a size distribution is defined.
Diameters are uniform within the selected bin. The largest bin ends at
5.8 mm, the upper limit of the terminal-speed fit. Both interpolation and
uniform sampling within bins are implementation choices.

Each arrival independently samples a material using `surface_weight`.
Weights need not sum to one. The defaults are water 0.37, dirt 0.21,
leaf 0.26, concrete 0.15, glass 0.005, and metal 0.005.
Plastic, asphalt, and asphalt-roof weights default to zero.

The azimuth is uniform over a circle. Radial distance is

    r = sqrt(r_min² + u(r_max² - r_min²)),  u uniform in [0, 1)

This makes positions uniform by area in an annulus, rather than
concentrating drops near the listener. Material weights describe fractions
of arrivals; they do not define spatial patches or physical surface areas.

## Drop velocity and excitation

The Dingle-Lee fit reproduced in [1, section 4.1.1] estimates terminal
speed from diameter d. In the following expressions, d is in millimetres
and the polynomial result is in centimetres per second:

    d <= 1.4: V_T = -17.8951 + 448.9498d + 16.3719d² - 45.9516d³
    d > 1.4:  V_T =  24.1660 + 448.8336d - 75.6265d² + 4.2695d³

Multiply by 0.01 to obtain m/s. For example, a 1 mm drop has a terminal
speed of about 4.015 m/s. Impact speed after falling height z is

    V = V_T sqrt(1 - exp(-2gz / V_T²))

The implementation uses `expm1f` for numerical accuracy at small heights.
The default height is 20 m. Manual arrivals supply their own velocity.

Mass is proportional to radius cubed, and kinetic energy is mV²/2.
The source amplitude follows the square root of relative kinetic energy:

    A = 0.035 × (drop_radius / 0.0005)^(3/2) × V / 4

This is a chosen conversion from impact energy to digital amplitude,
not a pressure calibration. It differs from the pressure amplitude model
in [1]. The coefficient 0.035 sets mix headroom, and the radius and speed
in the denominator define a 1 mm diameter reference drop at 4 m/s.

## Initial impact

Every moving drop produces

    x_I(t) = A exp(-βt) sin(2πft)

with f uniform between 1 and 16 kHz and β = 2f. These frequency and damping
choices follow [1, section 4.1.1]. This produces a brief impact impulse.
The engine uses this temporal mode, not the paper's complete dipole field,
water-hammer pressure amplitude, or geometry-dependent radiation model.
Water impacts additionally sample `water_impact_gain_min` to
`water_impact_gain_max`; other materials use gain one.

## Water bubbles

Each automatic water impact creates a bubble with the configured probability,
which defaults to 0.85. Bubble radius is sampled logarithmically from the
configured range, which defaults to 0.35 to 1.6 mm. This extends beyond the
0.16 to 0.47 mm range reported in [1, section 4.1.2] as an explicit sound-design
choice. A manually triggered water drop supplies any bubble radius from 0.16 to
4 mm, or zero for no bubble, and does not use the probability or radius range.

The resonance follows Minnaert's formula as presented in [1, equation 5]:

    f_B = sqrt(3γP / ρ_water) / (2πr_B)

A 0.4 mm bubble has a frequency of about 8208 Hz. Doubling its radius halves
its frequency. The smallest supported bubble remains below Nyquist.

The decay approximation from [4, section 3, equation 3] is

    β_B = 0.13/r_B + 0.0072/r_B^(3/2)

The bubble is a damped sinusoid with an onset 88 frames after impact,
approximately 2 ms. Each bubble samples a gain and decay scale from configured
ranges. Gain multiplies the water impact amplitude. The physical damping above
is divided by the decay scale; defaults from 3 to 8 produce longer tails.
Frequencies remain fixed during a bubble's lifetime. The pitch-rise model
described in [3] and [4] is not implemented. If added, instantaneous frequency
must be integrated to get phase; substituting f(t)t directly doubles a linear
chirp's slope.

## Solid surfaces

Dry solid impacts excite two additional damped modes. The following
presets list `(frequency Hz, damping per second)` for each mode, followed
by the first mode's amplitude relative to A and the initial impact gain.
The second mode has half the first mode's amplitude:

- Dirt: (450, 1200), (1100, 1800), resonance 0.35, impact 1.0.
- Leaf: (1800, 800), (4200, 1400), resonance 0.50, impact 1.0.
- Concrete: (1400, 1400), (3700, 2200), resonance 0.45, impact 1.0.
- Glass: (3200, 160), (7100, 260), resonance 0.325, impact 1.0.
- Metal: (1700, 90), (4300, 150), resonance 0.40, impact 1.0.
- Plastic: (220, 110), (650, 220), resonance 0.65, impact 0.50.
- Asphalt: (300, 1600), (900, 2600), resonance 0.25, impact 0.30.
- Asphalt roof: (140, 300), (420, 700), resonance 0.40, impact 0.25.

Each solid impact multiplies both frequencies by one uniform factor between
0.85 and 1.15. The presets represent different resonant responses, but are not
measured material constants or solutions for a particular object shape. Dry
surfaces do not produce bubble modes. Wet solids and puddle formation are not
modeled.

Plastic and asphalt-roof sources then pass through two cascaded one-pole
low-pass filters at 1600 Hz and 900 Hz respectively. The asphalt-roof filter
represents sound transmitted through the roof and ceiling to an indoor
listener. The asphalt surface has no transmission filter and preserves the
former asphalt-roof preset.

[3] demonstrates a finite-difference metal-bar model. This implementation
uses a small modal approximation to keep work bounded on ESP32, not that
bar solver. [1] uses recorded material textures; the engine does not use
those recordings or reproduce its VMD decomposition.

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

`stereo_width_m` is the distance between ears or microphones. It defaults
to 0.18 m and accepts 0 to 0.5 m. `head_amount` defaults to one; zero
disables the head filter and diffraction. `rear_amount` independently
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

Thunder is a triggered event, not a continuous layer. The model follows the
four signal-based components of Fineberg, Walters, and Reiss [7, sections 3.1
to 3.4], including their post-survey revisions [7, section 6.1]. Every
component starts from uniform white noise and ends with a linear ramp to zero.

- Clap: 1 to 5 strikes. The first starts at once; the others start uniformly
  within the first second. Each strike samples r uniform in [0, 1). Its
  band-pass centre is 80 + 1200r Hz with Q 7, and the centre falls linearly
  to half over the strike. Its length is 240(1.4 - r)^5 ms, from 2.45 ms to
  1.29 s. Even strikes filter a noise burst. Odd strikes filter about 20
  impulses at random frames; each impulse has amplitude sqrt(length/60), so
  both kinds carry the same expected energy.
- Rumbler: two low-passed noise streams. The first is half-wave rectified.
  A sample-and-hold captures the second at 1 + 2.5g Hz, where g falls from
  1 to 0 with the gain ramp. The engine multiplies the two; the paper does not
  say how they combine. A 20 Hz DC blocker removes the rectifier's offset. The
  gain ramp lasts 9 s. The cutoff starts near 1 kHz and falls over 12 s, so it
  ends at a quarter of its start value when the gain reaches zero.
- Afterimage: noise low-passed at 33 Hz, falling to 1 Hz, times 80, times a
  second noise, clipped to [-1, 1], then band-passed at 333 Hz with Q 4. The
  gain ramp lasts 14 s.
- Deepener: low-pass 60 Hz, then high-pass 30 Hz, both Q 3; times 3.5,
  clipped to [-1, 1], then low-pass 80 Hz with Q 3. The gain ramp lasts 18.5 s.

The biquads use the RBJ cookbook forms. Time-varying cutoffs recompute every
32 frames to keep trigonometry out of the per-sample path.

The paper's listeners found deterministic envelopes and regular timing less
realistic [7, section 5.2]. Each strike therefore samples its strike count,
timings, and clap gains. It also scales each component's ramp length by
0.75 to 1.25, its gain by 0.7 to 1.3, the rumble cutoff by 0.7 to 1.3, and
the afterimage centre by 0.85 to 1.15. Component mix weights are sound design levels; the paper gives
no absolute gains for this filter chain. At 1 km and thunder gain one, the
worst peak over 20 seeds is about 0.68 of full scale.

Distance d, from 200 m to 15 km, sets a level `min(1, 1000/d)`. The rumbler,
afterimage, and deepener use that level; the clap uses its square, so a strike
8 km away is almost all rumble. A one-pole low-pass on the clap stands in for
air absorption, with cutoff `2×10^6/d` Hz limited to 250 to 8000 Hz. The
paper's distance delay adds silence before an event with no visual flash, so
the engine does not use it.

A constant-power pan places each strike by azimuth: the pan angle is
`π/4 × (1 + sin φ)`. The clap is mono before the pan. The rumbler,
afterimage, and deepener draw independent noise for each channel, which makes
them wide. Drops keep the spherical-head model; at kilometre distances its
cues reduce to level and delay a pan already supplies. The clap alone sends
to the shared reverb, scaled by thunder gain.

Automatic strikes start one at time zero, then use a Bernoulli trial per
frame at `thunder_rate_per_min / 60` per second. The comparison uses 32 bits,
so slow rates keep their resolution. Positions are uniform by area between
the distance bounds, with uniform azimuth. Two voices can overlap. A strike
that finds both busy is rejected and counted in `state.dropped_thunder`.

The paper's 0.6 s feedback delay, beach convolution reverb, and output
compressor are not implemented. The delay line alone would need about 105 KB
of floats. The shared reverb and master gain cover their roles.

## Weather controller

Optional intensity variation uses a three-state Markov chain. At each
`weather_step_s` interval, its transition probabilities are:

- From light: light 0.85, heavy 0.15, very heavy 0.
- From heavy: light 0.10, heavy 0.80, very heavy 0.10.
- From very heavy: light 0, heavy 0.15, very heavy 0.85.

The states target the configured minimum intensity, midpoint, and
maximum intensity. These probabilities are a chosen weather texture,
not fitted meteorological data. Self-transitions produce persistent
weather; transitions only move to adjacent states. At the default
8 s interval, expected uninterrupted dwell times are about 53, 40,
and 53 seconds. The first transition occurs after one full interval.

The initial state is whichever target is closest to `rain_intensity`,
with midpoint ties resolved upward. Actual intensity starts at the
requested value and approaches the target once per audio frame:

    α = 1 - exp(-1 / (rain_slew_s × sample_rate))
    I[n+1] = I[n] + α(target - I[n])

The default time constant is 2 s. This is an exponential approach,
not a fixed-duration ramp. It changes arrival rate and size probabilities;
existing drops retain their coefficients. With `vary_rain` disabled,
intensity remains fixed. With both bounds zero, varying rain stays silent.
The accumulator carries rounding error between frames so a slow transition
does not stall when an individual step is smaller than a float can represent.

## Weather modulation

`weather_mod_amount` routes the current rain intensity to sound parameters.
Each amount is an attenuverter from -1 to 1. Zero disconnects a route, positive
amounts follow intensity, and negative amounts invert it. Arrival rate and drop
size default to 1 to preserve the basic rain model; all other routes default to
zero.

For arrival rate, a positive amount blends between the configured maximum rate
and the intensity-scaled rate. A negative amount blends toward inverse
intensity. Drop size blends between the medium distribution and the light to
very-heavy intensity curve, or its inverse. Gain destinations use linear
modulation around their base setting at intensity 0.5. Distance and fall height
use the same rule in logarithmic space.

Surface modulation multiplies each base weight by `1 + amount × (2I-1)`, with a
0.001 floor, then normalizes all nine effective weights. The floor keeps a valid
distribution when every route reaches its negative extreme. A zero base weight
remains zero.

## API and limits

Initialize a `noise_config` with `noise_config_default`, then edit values
before calling `noise_init`. Configuration is copied into the engine.
A successful initialization resets all voices, random streams, filters,
and delay lines. Invalid configuration returns `NOISE_INVALID_CONFIG`
and leaves the engine unchanged. Do not render an uninitialized engine.

`noise_fill` takes a count of stereo frames, writes twice that many
interleaved int16 samples, and returns the frame count. A zero-frame call
changes no state and may use a null output pointer. Other calls require
valid storage. Configuration is immutable after initialization; do not
mutate engine fields. The public `state` is for inspection by the owning
thread.

`noise_trigger_thunder` accepts a strike distance from 200 m to 15 km and an
angle from negative 2π to positive 2π. Invalid strikes return
`NOISE_INVALID_STRIKE` and leave the engine unchanged. A strike with both
voices busy returns `NOISE_VOICE_LIMIT`.

`noise_trigger_drop` accepts a physical drop at the listener's arrival
time. It validates radius (0.4 to 2.9 mm), velocity (0 to 12 m/s), distance
(0.25 to 100 m), angle (negative 2π to positive 2π), and bubble constraints.
Zero velocity succeeds without allocating a voice. Invalid drops leave
the engine unchanged. Call it between fills from the same audio thread.
Do not call it concurrently with rendering.

Configuration ranges are:

- Ambient, rain, master, and reverb gains: 0 to 1 each.
- Wind brightness, gust depth, and stereo width: 0 to 1. Gust rate: 0.01 to
  2 Hz.
- Cricket call rate: 0.05 to 10 chirps/s per cricket. Pitch: 2 to 8 kHz.
  Pitch variation and stereo width: 0 to 1. Distance bounds: 0.25 to 100 m,
  ordered; defaults 2 and 15 m.
- Cicada pitch: 2 to 10 kHz. Pulse rate: 10 to 120 Hz. Texture and stereo
  width: 0 to 1.
- Thunder gain: 0 to 1, default 0. Strike rate: 0 to 20 per minute, default 2.
  Distance bounds: 200 m to 15 km, ordered; defaults 1 and 8 km.
- Initial, minimum, and maximum rain intensity: 0 to 1; minimum must not
  exceed maximum. Initial intensity must lie within bounds when varying.
- `vary_rain`: 0 or 1.
- Weather step: 0.1 to 3600 s. Slew time constant: 0.01 to 60 s.
- Maximum arrival rate: 0 to 2000/s.
- Material weights: 0 to 1000 each, at least one positive.
- Distance bounds: 0.25 to 100 m, ordered; equal bounds make a ring.
- Falling height: 0.01 to 1000 m.
- Stereo width: 0 to 0.5 m; head and rear amounts: 0 to 1.
- Water impact gain bounds: 0 to 2, ordered. Bubble probability: 0 to 1.
- Automatic bubble radius bounds: 0.16 to 4 mm, ordered.
- Bubble gain bounds: 0 to 8, ordered. Decay scale bounds: 0.25 to 20,
  ordered.
- Weather modulation amounts: -1 to 1 each.

Eight separate random streams drive ambient samples, wind, crickets, cicadas,
thunder, arrivals, drop properties, and weather. Enabling ambient sound or
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
The engine occupies 58,280 bytes with the tested host ABI, plus 1,024
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
hum fundamentals and harmonics, spectral slopes, rain count statistics,
weather bounds, voice exhaustion and recycling, silence, block-size
independence, malformed CLI values, WAV headers, all nine surfaces, and
linking the sketch against the C engine. Spatial checks cover rendered
phase, ear symmetry, head shelf gain, width bypass, distance gain,
reverb-send independence, rear filtering, and maximum delay bounds.
Thunder checks cover validation, voice limits, retirement to exact silence,
low-frequency dominance, distance filtering, panning, automatic rate, and
rain-stream independence.
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
7. Eva Fineberg, Jack Walters, and Joshua D. Reiss. *Advances in Thunder Sound
   Synthesis*. Audio Engineering Society, 2022.
   [arXiv:2204.08026](https://arxiv.org/abs/2204.08026).
   Sections 3.1 to 3.4 describe the multi-strike, rumbler, afterimage, and
   deepener components. Section 6.1 supplies the revised clap Q, clap centre,
   and deepener high-pass. Its delay, convolution reverb, compressor, and
   Web Audio panner are not used.
