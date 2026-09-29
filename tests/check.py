"""Build and check the shared core and desktop WAV harness using the stdlib."""

from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import wave

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "src" / "NoiseMachine"


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def main():
    with tempfile.TemporaryDirectory(prefix="noisemachine-") as directory:
        work = Path(directory)
        cc = os.environ.get("CC", "cc")
        flags = ["-std=c11", "-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror", f"-I{CORE}"]
        run(cc, *flags, str(ROOT / "tests/test_core.c"), str(CORE / "noise_core.c"),
            "-lm", "-o", str(work / "test_core"))
        run(str(work / "test_core"))
        host = work / "noise_host"
        run(cc, *flags, str(ROOT / "host/main.c"), str(CORE / "noise_core.c"),
            "-lm", "-o", str(host))
        destination = work / "audio.wav"
        for kind in ("white", "pink", "hum50", "hum60", "wind", "crickets", "cicadas",
                     "rain"):
            run(str(host), "-k", kind, "-d", "0.125", str(destination), stdout=subprocess.DEVNULL)
            with wave.open(str(destination)) as wav:
                assert wav.getnchannels() == 2
                assert wav.getsampwidth() == 2
                assert wav.getframerate() == 44100
                assert wav.getnframes() == int(0.125 * 44100)
                samples = wav.readframes(wav.getnframes())
                assert any(samples)
            raw = destination.read_bytes()
            assert struct.unpack_from("<I", raw, 4)[0] == len(raw) - 8
            assert struct.unpack_from("<I", raw, 40)[0] == len(samples)
        for material in ("water", "dirt", "leaf", "concrete", "glass", "metal",
                         "plastic", "asphalt", "asphalt-roof", "mixed"):
            result = run(str(host), "-k", "rain", "-r", "1", "-m", material,
                         "-d", "3", str(destination), capture_output=True, text=True)
            assert "capacity losses: 0" in result.stdout, result.stdout
            assert "clipped samples: 0" in result.stdout, result.stdout
        run(str(host), "-k", "rain", "-k", "hum50", "-v", "-d", "20",
            str(destination), stdout=subprocess.DEVNULL)
        for args in (("-d", "nan"), ("-d", "inf"), ("-d", "1e300"), ("-d", "-1"),
                     ("-d", ""), ("-d", "0"), ("-s", "-1"), ("-s", ""),
                     ("-s", "4294967296"), ("-s", "1x"), ("-r", "nan"), ("-r", "1.1"),
                     ("-k", "bad"), ("-m", "bad"), ("-v", "-l", "0.9", "-u", "0.2"),
                     ("-v", "-r", "0"), ("-z", "1"), ("-g", "1e30"), ("-n", "2001"),
                     ("-b", "0.51"), ("-a", "-0.1"), ("-f", "nan")):
            destination.write_bytes(b"preserve existing file")
            result = subprocess.run((str(host), *args, str(destination)), capture_output=True)
            assert result.returncode != 0, args
            assert result.stderr, args
            assert destination.read_bytes() == b"preserve existing file", args
        # Link the actual sketch as C++ against a C-compiled engine without an SDK.
        cxx = os.environ.get("CXX", "c++")
        run(cc, *flags, "-c", str(CORE / "noise_core.c"), "-o", str(work / "core.o"))
        stub = work / "sketch.cpp"
        stub.write_text(
            'struct SerialStub { void begin(int) {} void println(const char *) {} };\n'
            'SerialStub Serial;\n'
            f'#include "{CORE / "NoiseMachine.ino"}"\n'
            'int main() { setup(); loop(); }\n'
        )
        run(cxx, "-std=c++11", "-Wall", "-Wextra", "-Werror", str(stub),
            str(work / "core.o"), "-o", str(work / "sketch"))
        run(str(work / "sketch"))
        if sys.platform == "darwin":
            run(cc, *flags, "-fobjc-arc", str(ROOT / "host/gui.m"),
                str(CORE / "noise_core.c"), "-lm", "-framework", "Cocoa",
                "-framework", "AudioToolbox", "-o", str(work / "noise_gui"))
        print("WAV, CLI, all surfaces, and C/C++ linkage checks passed")


if __name__ == "__main__":
    main()
