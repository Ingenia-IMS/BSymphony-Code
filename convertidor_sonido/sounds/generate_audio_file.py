import argparse
import struct
from pathlib import Path
import subprocess


INPUT_FILE  = "FireForest.mp3"
OUTPUT_H    = "FireForest.h"
SAMPLE_RATE = 48000

# Llamada a ffmpeg: convierte a RAW PCM 16-bit signed little-endian mono
# Llamada a ffmpeg: convierte a RAW PCM 16-bit signed little-endian mono
def convert_to_raw(input_file: str, sample_rate: int) -> bytes:
    result = subprocess.run([
        "ffmpeg", "-i", input_file,
        "-ar", str(sample_rate),
        "-ac", "1",
        "-f", "s16le",
        "-"              # salida por stdout
    ], capture_output=True)

    if result.returncode != 0:
        print(result.stderr.decode())
        exit(1)

    raw = result.stdout
    samples = struct.unpack(f"<{len(raw)//2}h", raw)
    return samples

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert a 16-bit PCM RAW file into a C header array."
    )
    parser.add_argument("input_file", help="Path to input .raw file")
    parser.add_argument(
        "output_file",
        nargs="?",
        default=None,
        help="Path to output .h file (optional). If omitted, uses <input_base>.h",
    )
    return parser.parse_args()


def resolve_output_path(input_path: Path, output_arg: str | None) -> Path:
    if output_arg:
        return Path(output_arg)
    return input_path.with_suffix(".h")


def main() -> None:
    args = parse_args()
    input_path = Path(args.input_file)
    output_path = resolve_output_path(input_path, args.output_file)

    samples = convert_to_raw(args.input_file, SAMPLE_RATE)

    print(f"Muestras: {len(samples)}, duración: {len(samples)/SAMPLE_RATE:.2f} s")

    with output_path.open("w", encoding="utf-8") as out:
        out.write("#pragma once\n#include <stdint.h>\n\n")
        out.write("const int16_t audio_data[] = {\n    ")
        out.write(",\n    ".join(
            ", ".join(f"0x{s & 0xFFFF:04X}" for s in samples[i:i + 16])
            for i in range(0, len(samples), 16)
        ))
        out.write("\n};\n")
        out.write(f"const size_t audio_data_len = {len(samples)};\n")

    print(f"Generated: {output_path}")


if __name__ == "__main__":
    print("Entrando")
    main()

    