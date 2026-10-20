"""Compara la búsqueda secuencial de archivos, la búsqueda por grupos de claves
y la variante híbrida con k=8,16,32. Calcula speedup y eficiencia usando la
mediana del tiempo secuencial como referencia.

Ejemplo desde la raíz:
python3 scripts/compare_distributed.py --bits 20 --key 1048575 --processes 4
Guarda el código usado, los datos de las pruebas, las salidas completas
y una tabla de resultados en results/runs/.
"""

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import statistics
import subprocess
import tarfile
import tempfile


def unsigned_integer(text):
    if not text.isascii() or not text.isdecimal() or int(text) >= (1 << 64):
        raise argparse.ArgumentTypeError("Debe ser un entero decimal uint64_t.")
    return int(text)


def positive_integer(text):
    value = unsigned_integer(text)
    if value == 0:
        raise argparse.ArgumentTypeError("Debe ser un entero decimal positivo.")
    return value


def read_metric(output, label):
    """Lee una medición de la salida, como el tiempo o la cantidad de claves probadas."""
    metric_match = re.search(rf"^{re.escape(label)}: ([0-9.]+)", output, re.MULTILINE)
    if metric_match is None:
        raise ValueError(f"No se encontro la metrica {label} en la salida.")
    return metric_match.group(1)


def run_measurement(command, log_path, expected_key, timeout):
    """Ejecuta una búsqueda, guarda su salida y comprueba que recuperó la clave esperada."""
    process_result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, timeout=timeout)
    log_path.write_text(process_result.stdout, encoding="utf-8")
    if process_result.returncode != 0:
        raise RuntimeError(f"Fallo la busqueda; revisar {log_path}")
    found_candidate_match = re.search(r"^Clave encontrada: candidata (\d+)(?: |$)",
                                      process_result.stdout, re.MULTILINE)
    if found_candidate_match is None or int(found_candidate_match.group(1)) != expected_key:
        raise RuntimeError(f"La busqueda no recupero la clave esperada: {log_path}")
    elapsed_seconds = float(read_metric(process_result.stdout, "Tiempo"))
    candidates_tested = int(read_metric(process_result.stdout, "Candidatas probadas"))
    if elapsed_seconds <= 0 or candidates_tested <= 0:
        raise RuntimeError(f"Medicion no valida: {log_path}")
    return process_result.stdout, elapsed_seconds, candidates_tested


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file", type=Path, default=Path("mensaje.txt"))
    parser.add_argument("--bits", type=positive_integer, default=20)
    parser.add_argument("--key", type=unsigned_integer)
    parser.add_argument("--processes", type=positive_integer, default=4)
    parser.add_argument("--repetitions", type=positive_integer, default=5)
    parser.add_argument("--k", type=positive_integer, nargs="+", default=[8, 16, 32])
    parser.add_argument("--seed", type=unsigned_integer, default=1)
    parser.add_argument("--mpiexec", default=os.environ.get("MPIEXEC", "mpirun"))
    parser.add_argument("--timeout", type=positive_integer, default=120)
    parser.add_argument("--skip-sequential", action="store_true",
                        help="Compara solo MPI cuando la busqueda secuencial seria demasiado larga.")
    command_options = parser.parse_args()
    if command_options.bits > 56:
        parser.error("bits debe estar en 1..56.")
    secret_candidate = (command_options.key if command_options.key is not None
                        else (1 << command_options.bits) - 1)
    if secret_candidate >= (1 << command_options.bits):
        parser.error("La clave debe ser menor que 2^bits.")
    project_directory = Path(__file__).resolve().parents[1]
    message_path = command_options.file.resolve()
    message_bytes = message_path.read_bytes()
    if not 0 < len(message_bytes) <= 4096 or b"es una prueba de" not in message_bytes:
        parser.error("El archivo debe tener 1..4096 bytes y contener 'es una prueba de'.")
    run_parent = project_directory / "results/runs"
    run_parent.mkdir(parents=True, exist_ok=True)
    run_directory = Path(tempfile.mkdtemp(
        prefix="hybrid-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-"),
        dir=run_parent,
    ))
    build_directory = run_directory / "build"
    with (run_directory / "build.log").open("w") as build_log:
        subprocess.run(["make", "-j4", f"BUILD_DIR={build_directory}", "all"],
                       cwd=project_directory, stdout=build_log, stderr=subprocess.STDOUT,
                       check=True)
    metadata = {
        "bits": command_options.bits, "key": secret_candidate, "processes": command_options.processes,
        "repetitions": command_options.repetitions, "k": command_options.k, "seed": command_options.seed,
        "message_path": str(message_path),
        "message_sha256": hashlib.sha256(message_bytes).hexdigest(),
        "platform": platform.platform(), "logical_cpu_count": os.cpu_count(),
        "mpiexec": command_options.mpiexec,
        "includes_sequential": not command_options.skip_sequential,
        "note": "Cada ejecucion genera su propio IV; se comparan dimensiones y recorrido.",
    }
    for name, command in {
        "git_head": ["git", "rev-parse", "HEAD"],
        "git_status": ["git", "status", "--short"],
        "build_configuration": ["make", "-s", f"BUILD_DIR={build_directory}", "print-config"],
        "c_compiler": shlex.split(os.environ.get("CC", "cc")) + ["--version"],
        "mpi_compiler": shlex.split(os.environ.get("MPICC", "mpicc")) + ["--version"],
        "mpi_launcher": shlex.split(command_options.mpiexec) + ["--version"],
    }.items():
        metadata[name] = subprocess.run(command, cwd=project_directory,
                                        check=True, capture_output=True, text=True).stdout
    (run_directory / "metadata.json").write_text(
        json.dumps(metadata, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (run_directory / "message.bin").write_bytes(message_bytes)
    with tarfile.open(run_directory / "source.tar.gz", "w:gz") as source_archive:
        for name in ["Makefile", "README.md", "src", "scripts", "tests", "guides"]:
            source_archive.add(project_directory / name, arcname=name)
    mpi_launcher = shlex.split(command_options.mpiexec) + ["-np", str(command_options.processes)]
    variants = ([] if command_options.skip_sequential else [("sequential", 0)])
    variants += [("blocks", 0)] + [("hybrid", value) for value in dict.fromkeys(command_options.k)]
    variant_commands = {}
    for approach, sequential_trials_per_turn in variants:
        binary_name = {"sequential": "aes_search_file", "blocks": "aes_search_mpi_blocks",
                       "hybrid": "aes_search_mpi_hybrid"}[approach]
        command = [] if approach == "sequential" else mpi_launcher.copy()
        command += [str(build_directory / "bin" / binary_name), str(message_path),
                    str(secret_candidate), str(command_options.bits)]
        if approach == "hybrid":
            command += [str(sequential_trials_per_turn), str(command_options.seed)]
        variant_commands[(approach, sequential_trials_per_turn)] = command
        run_measurement(command, run_directory / f"warmup_{approach}_{sequential_trials_per_turn}.log",
                        secret_candidate, command_options.timeout)

    measurements = []
    csv_columns = ["approach", "k", "processes", "repetition", "seconds", "candidates",
                   "sequential_candidates", "permuted_candidates"]
    with (run_directory / "measurements.csv").open("w", newline="") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=csv_columns)
        writer.writeheader()
        for repetition in range(command_options.repetitions):
            # Cambia cuál versión se prueba primero para no favorecer siempre a la misma.
            first_variant_index = repetition % len(variants)
            for approach, sequential_trials_per_turn in (
                variants[first_variant_index:] + variants[:first_variant_index]
            ):
                output, elapsed_seconds, candidates_tested = run_measurement(
                    variant_commands[(approach, sequential_trials_per_turn)],
                    run_directory / f"{approach}_{sequential_trials_per_turn}_{repetition + 1}.log",
                    secret_candidate, command_options.timeout,
                )
                sequential_candidates = (candidates_tested if approach != "hybrid"
                                         else int(read_metric(output, "Candidatas secuenciales")))
                permuted_candidates = (0 if approach != "hybrid"
                                       else int(read_metric(output, "Candidatas permutadas")))
                if (sequential_candidates + permuted_candidates != candidates_tested
                        or candidates_tested > (1 << command_options.bits)):
                    raise RuntimeError("Conteo de candidatas inconsistente.")
                measurement = {
                    "approach": approach,
                    "k": sequential_trials_per_turn,
                    "processes": 1 if approach == "sequential" else command_options.processes,
                    "repetition": repetition + 1,
                    "seconds": elapsed_seconds,
                    "candidates": candidates_tested,
                    "sequential_candidates": sequential_candidates,
                    "permuted_candidates": permuted_candidates,
                }
                writer.writerow(measurement)
                csv_file.flush()
                measurements.append(measurement)
    summaries = []
    for approach, sequential_trials_per_turn in variants:
        variant_measurements = [
            measurement for measurement in measurements
            if measurement["approach"] == approach and measurement["k"] == sequential_trials_per_turn
        ]
        summaries.append({
            "approach": approach,
            "k": sequential_trials_per_turn,
            "processes": 1 if approach == "sequential" else command_options.processes,
            "median_seconds": statistics.median(measurement["seconds"] for measurement in variant_measurements),
            "median_candidates": statistics.median(measurement["candidates"] for measurement in variant_measurements),
        })
    sequential_seconds = next((summary["median_seconds"] for summary in summaries
                               if summary["approach"] == "sequential"), None)
    summary_columns = ["approach", "k", "processes", "median_seconds", "median_candidates",
                       "speedup", "efficiency"]
    print("Variante | k | Mediana (s) | Candidatas (mediana) | Speedup | Eficiencia")
    with (run_directory / "summary.csv").open("w", newline="") as summary_file:
        writer = csv.DictWriter(summary_file, fieldnames=summary_columns)
        writer.writeheader()
        for summary in summaries:
            # Sin una medición secuencial no se puede calcular esta comparación.
            summary["speedup"] = (sequential_seconds / summary["median_seconds"]
                                  if sequential_seconds is not None else None)
            summary["efficiency"] = (summary["speedup"] / summary["processes"]
                                     if summary["speedup"] is not None else None)
            writer.writerow(summary)
            speedup_text = f"{summary['speedup']:.3f}" if summary["speedup"] is not None else "sin referencia"
            efficiency_text = (f"{100 * summary['efficiency']:.1f}%"
                               if summary["efficiency"] is not None else "sin referencia")
            print(f"{summary['approach']} | {summary['k'] or '-'} | "
                  f"{summary['median_seconds']:.6f} | {summary['median_candidates']:.0f} | "
                  f"{speedup_text} | {efficiency_text}")
    print(f"Resultados: {run_directory}")


main()
