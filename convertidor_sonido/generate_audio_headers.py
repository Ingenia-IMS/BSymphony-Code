from __future__ import annotations

import argparse
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path


# Cambia este valor si quieres dejar fija la frecuencia de muestreo por defecto.
DEFAULT_SAMPLE_RATE = 12000

# Extensiones de audio aceptadas.
SUPPORTED_EXTENSIONS = {".mp3", ".wav", ".flac", ".ogg", ".m4a", ".aac"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convierte todos los audios de una carpeta en archivos .h con muestras PCM mono 16-bit."
    )
    parser.add_argument(
        "input_dir",
        type=Path,
        help="Carpeta de entrada con archivos de audio",
    )
    parser.add_argument(
        "output_dir",
        type=Path,
        help="Carpeta de salida donde se generarán los .h",
    )
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=DEFAULT_SAMPLE_RATE,
        help=f"Frecuencia de muestreo de salida en Hz (por defecto: {DEFAULT_SAMPLE_RATE})",
    )
    parser.add_argument(
        "--recursive",
        action="store_true",
        help="Busca archivos de audio también en subcarpetas",
    )
    parser.add_argument(
        "--clean-output",
        action="store_true",
        help="Borra la carpeta de salida antes de generar los archivos",
    )
    return parser.parse_args()


def check_ffmpeg_available() -> None:
    try:
        result = subprocess.run(
            ["ffmpeg", "-version"],
            capture_output=True,
            text=True,
            check=False,
        )
    except FileNotFoundError:
        print("Error: ffmpeg no está instalado o no está en el PATH.")
        sys.exit(1)

    if result.returncode != 0:
        print("Error: ffmpeg no responde correctamente.")
        print(result.stderr)
        sys.exit(1)


def sanitize_stem(name: str) -> str:
    import re

    # Separa CamelCase/PascalCase, pero evita destrozar palabras totalmente en mayúsculas
    if not name.isupper():
        name = re.sub(r"(?<=[a-z0-9])(?=[A-Z])", "_", name)

    name = name.lower()
    name = re.sub(r"[^a-z0-9]", "_", name)
    name = re.sub(r"_+", "_", name)
    name = name.strip("_")

    if not name:
        name = "audio"

    if name[0].isdigit():
        name = f"audio_{name}"

    return name

def find_audio_files(input_dir: Path, recursive: bool) -> list[Path]:
    if recursive:
        candidates = input_dir.rglob("*")
    else:
        candidates = input_dir.iterdir()

    audio_files = [
        path for path in candidates
        if path.is_file() and path.suffix.lower() in SUPPORTED_EXTENSIONS
    ]

    return sorted(audio_files)


def convert_to_samples(input_file: Path, sample_rate: int) -> tuple[int, ...]:
    result = subprocess.run(
        [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(input_file),
            "-ar",
            str(sample_rate),
            "-ac",
            "1",
            "-f",
            "s16le",
            "-",
        ],
        capture_output=True,
        check=False,
    )

    if result.returncode != 0:
        stderr_text = result.stderr.decode(errors="replace")
        raise RuntimeError(
            f"No se pudo convertir '{input_file.name}' con ffmpeg.\n{stderr_text}"
        )

    raw = result.stdout

    if len(raw) % 2 != 0:
        raise RuntimeError(
            f"El audio convertido de '{input_file.name}' tiene un número de bytes inválido."
        )

    sample_count = len(raw) // 2
    return struct.unpack(f"<{sample_count}h", raw)


def format_samples_as_c_array(samples: tuple[int, ...], values_per_line: int = 12) -> str:
    lines: list[str] = []

    for i in range(0, len(samples), values_per_line):
        chunk = samples[i:i + values_per_line]
        rendered = ", ".join(str(sample) for sample in chunk)
        lines.append(f"    {rendered}")

    if not lines:
        return ""

    return ",\n".join(lines)


def build_header_text(symbol_base: str, samples: tuple[int, ...], sample_rate: int) -> str:
    array_name = f"audio_{symbol_base}"
    len_name = f"{array_name}_len"
    rate_name = f"{array_name}_sample_rate"

    array_body = format_samples_as_c_array(samples)

    return f"""#pragma once
#include <stdint.h>
#include <stddef.h>

static const int16_t {array_name}[] = {{
{array_body}
}};

static const size_t {len_name} = {len(samples)};
static const uint32_t {rate_name} = {sample_rate};
"""


def write_header_file(output_file: Path, content: str) -> None:
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(content, encoding="utf-8", newline="\n")


def main() -> None:
    script_dir = Path(__file__).resolve().parent

    # Si no hay argumentos → modo automático (Run en VSCode / doble click)
    if len(sys.argv) == 1:
        print("Modo automático (sin argumentos)")
        input_dir = script_dir / "audio_in"
        output_dir = script_dir / "audio_out"
        sample_rate = DEFAULT_SAMPLE_RATE
        recursive = False
        clean_output = False
    else:
        args = parse_args()
        input_dir = args.input_dir
        output_dir = args.output_dir
        sample_rate = args.sample_rate
        recursive = args.recursive
        clean_output = args.clean_output

        # Si te pasan rutas relativas por terminal, las resolvemos también
        if not input_dir.is_absolute():
            input_dir = (Path.cwd() / input_dir).resolve()
        if not output_dir.is_absolute():
            output_dir = (Path.cwd() / output_dir).resolve()

    if sample_rate <= 0:
        print("Error: el sample rate debe ser un entero positivo.")
        sys.exit(1)

    print(f"Carpeta script:  {script_dir}")
    print(f"Carpeta entrada: {input_dir}")
    print(f"Carpeta salida:  {output_dir}")

    if not input_dir.exists() or not input_dir.is_dir():
        print(f"Error: la carpeta de entrada no existe o no es válida: {input_dir}")
        sys.exit(1)

    check_ffmpeg_available()

    if clean_output and output_dir.exists():
        shutil.rmtree(output_dir)

    output_dir.mkdir(parents=True, exist_ok=True)

    audio_files = find_audio_files(input_dir, recursive)

    if not audio_files:
        print("No se encontraron archivos de audio compatibles en la carpeta de entrada.")
        sys.exit(0)

    print(f"Frecuencia de muestreo seleccionada: {sample_rate} Hz")
    print(f"Archivos encontrados: {len(audio_files)}")

    generated_count = 0

    for audio_file in audio_files:
        symbol_base = sanitize_stem(audio_file.stem)
        output_file = output_dir / f"{symbol_base}.h"

        try:
            samples = convert_to_samples(audio_file, sample_rate)
            header_text = build_header_text(symbol_base, samples, sample_rate)
            write_header_file(output_file, header_text)
        except Exception as exc:
            print(f"[ERROR] {audio_file.name}: {exc}")
            continue

        duration_s = len(samples) / sample_rate if sample_rate > 0 else 0.0
        print(
            f"[OK] {audio_file.name} -> {output_file.name} | "
            f"{len(samples)} muestras | {duration_s:.2f} s"
        )
        generated_count += 1

    print(f"\nGenerados correctamente: {generated_count} de {len(audio_files)}")

if __name__ == "__main__":
    main()