"""Regresiones de CLI y terminacion MPI; ejecutar mediante make test/check."""

import argparse
from pathlib import Path
import re
import shlex
import subprocess


def run_program(command, successful=True):
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    if (result.returncode == 0) != successful:
        raise AssertionError(
            f"Salida inesperada ({result.returncode}): {command}\n"
            f"{result.stdout}\n{result.stderr}"
        )
    return result.stdout


def verify_search(command, candidate, message, candidate_count=None):
    output = run_program(command)
    if f"Clave encontrada: candidata {candidate}\n" not in output and not re.search(
        rf"Clave encontrada: candidata {candidate} \(proceso \d+\)", output
    ):
        raise AssertionError(f"Candidata incorrecta: {command}\n{output}")
    if f"Mensaje: {message}\n" not in output:
        raise AssertionError(f"Mensaje incorrecto: {command}\n{output}")
    if candidate_count is not None:
        if f"Candidatas probadas: {candidate_count}\n" not in output:
            raise AssertionError(f"Conteo incorrecto: {command}\n{output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, default=Path("build/bin"))
    parser.add_argument("--mpi", action="store_true")
    parser.add_argument("--mpiexec", default="mpirun")
    options = parser.parse_args()
    binary_directory = options.bin_dir.resolve()
    invalid_arguments = [
        ["0"], ["41"], ["-1"], ["+1"], [" 1"], ["1x"], [""],
        ["18446744073709551616"], ["2", "4"], ["2", "-1"],
        ["2", "0", ""], ["2", "0", "a" * 257], ["2", "0", "a", "extra"],
    ]
    if options.mpi:
        executable = str(binary_directory / "aes_search_mpi")
        launcher = shlex.split(options.mpiexec)
        # Incluye ranks sin trabajo, reparto desigual, varias rondas y rank ganador != 0.
        for processes, unknown_bits, secret_candidate in [
            (1, 12, 4095), (2, 12, 0), (2, 16, 50000),
            (3, 13, 8191), (3, 16, 65535), (4, 1, 1),
        ]:
            command = launcher + ["-np", str(processes), executable,
                                  str(unknown_bits), str(secret_candidate)]
            expected_count = (1 << unknown_bits) if secret_candidate == (1 << unknown_bits) - 1 else None
            verify_search(command, secret_candidate, "Puedes lograrlo!", expected_count)
        for arguments in invalid_arguments:
            run_program(launcher + ["-np", "2", executable, *arguments], successful=False)
        print("OK: MPI con 1/2/3/4 procesos, rondas, ranks sin trabajo y argumentos invalidos.")
    else:
        ecb_output = run_program([str(binary_directory / "aes_search_ecb")])
        if "Clave encontrada: 12345\n" not in ecb_output:
            raise AssertionError(ecb_output)
        executable = str(binary_directory / "aes_search_ctr")
        for unknown_bits, secret_candidate in [(1, 0), (1, 1), (12, 4095), (16, 50000), (40, 0)]:
            verify_search([executable, str(unknown_bits), str(secret_candidate)],
                          secret_candidate, "Puedes lograrlo!", secret_candidate + 1)
        for message in ["abcdefgh", "a" * 256, "Mensaje con espacios y acentos: éxito"]:
            verify_search([executable, "8", "255", message], 255, message, 256)
        verify_search([executable, "8"], 255, "Puedes lograrlo!", 256)
        for arguments in invalid_arguments:
            run_program([executable, *arguments], successful=False)
        print("OK: referencia ECB, busqueda CTR, limites y argumentos invalidos.")


main()
