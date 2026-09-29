# NoiseMachine

## Test noise generation on macOS

The host harness builds on this computer. It uses the same C code as the
ESP32 firmware (`src/NoiseMachine/noise_core.c`) and writes a WAV file.

Build:

    make -C host

Run:

    ./host/noise_host [options] OUT.wav

Options:

- `-k white|pink` - noise type. Default: pink.
- `-s SEED` - random seed. Same seed gives the same output. Default: 1.
- `-d SECONDS` - length in seconds. Default: 10.

Play the result:

    afplay OUT.wav

`make -C host run` builds, writes `out/pink.wav`, and plays it.
