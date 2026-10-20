"""Comprueba los programas y revisa que todos los trabajadores puedan terminar.
Se ejecuta con make test o make check.
"""

import argparse
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

PROJECT_MESSAGE = "Esta es una prueba de proyecto 2"


def run_program(command, successful=True):
    process_result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    if (process_result.returncode == 0) != successful:
        raise AssertionError(
            f"Salida inesperada ({process_result.returncode}): {command}\n"
            f"{process_result.stdout}\n{process_result.stderr}"
        )
    return process_result.stdout


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
    return output


def verify_hybrid_statistics(output, unknown_bits, process_count, secret_candidate):
    candidates_tested = int(re.search(r"Candidatas probadas: (\d+)", output).group(1))
    sequential_candidates = int(re.search(r"Candidatas secuenciales: (\d+)", output).group(1))
    permuted_candidates = int(re.search(r"Candidatas permutadas: (\d+)", output).group(1))
    if (candidates_tested != sequential_candidates + permuted_candidates
            or not 0 < candidates_tested <= (1 << unknown_bits)):
        raise AssertionError(f"Conteo hibrido invalido:\n{output}")
    block_size = ((1 << unknown_bits) + process_count - 1) // process_count
    if f"(proceso {secret_candidate // block_size})" not in output:
        raise AssertionError(f"Trabajador propietario incorrecto:\n{output}")


def check_required_project_cases(executable, mpi_launcher, message_path,
                                 sequential_trials_per_turn=None):
    """Comprueba las tres claves del enunciado con cuatro trabajadores y 56 bits."""
    for secret_candidate in [123456, 18014398509481984, 18014398509481985]:
        command = mpi_launcher + ["-np", "4", executable, str(message_path),
                                  str(secret_candidate), "56"]
        if sequential_trials_per_turn is not None:
            command += [str(sequential_trials_per_turn), "1"]
        output = verify_search(command, secret_candidate, PROJECT_MESSAGE)
        expected_key = secret_candidate.to_bytes(16, "big").hex()
        if f"Clave AES-128: {expected_key}\n" not in output:
            raise AssertionError(f"Conversion incorrecta de la clave a 16 bytes:\n{output}")
        if f"Texto recuperado: {PROJECT_MESSAGE}\n" not in output:
            raise AssertionError(f"Descifrado inicial incorrecto:\n{output}")
        if sequential_trials_per_turn is not None:
            verify_hybrid_statistics(output, 56, 4, secret_candidate)
        elif f"(proceso {secret_candidate // (1 << 54)})" not in output:
            raise AssertionError(f"Reparto de las claves del enunciado incorrecto:\n{output}")


def check_sequential_file(binary_directory):
    """La referencia secuencial debe usar el mismo archivo y frase que MPI."""
    executable = str(binary_directory / "aes_search_file")
    with tempfile.TemporaryDirectory(prefix="aes-sequential-file-") as directory:
        message_path = Path(directory) / "mensaje.txt"
        message_path.write_text(PROJECT_MESSAGE + "\r\n", encoding="utf-8")
        for unknown_bits, secret_candidate in [(1, 0), (1, 1), (12, 4095), (56, 123456)]:
            output = verify_search(
                [executable, str(message_path), str(secret_candidate), str(unknown_bits)],
                secret_candidate, PROJECT_MESSAGE, secret_candidate + 1,
            )
            if "Ejecucion: secuencial\n" not in output:
                raise AssertionError(f"Tipo de ejecucion incorrecto:\n{output}")
        verify_search([executable, str(message_path), "0"], 0, PROJECT_MESSAGE, 1)
        for arguments in [[], [str(message_path)], [str(message_path), "4", "2"],
                          [str(message_path), "0", "57"],
                          [str(message_path), "-1", "8"],
                          [str(message_path), "+1", "8"],
                          [str(message_path), " 1", "8"],
                          [str(message_path), "1x", "8"],
                          [str(message_path), "", "8"],
                          [str(message_path), "0", "0"],
                          [str(message_path), "0", "8", "extra"],
                          [str(message_path), str(1 << 64), "8"],
                          [str(message_path.with_name("no_existe.txt")), "0", "8"]]:
            run_program([executable, *arguments], successful=False)
        for invalid_text in ["", "Mensaje sin la frase requerida", "a" * 4097]:
            message_path.write_text(invalid_text, encoding="utf-8")
            run_program([executable, str(message_path), "0", "8"], successful=False)
        maximum_message = "x" * (4096 - len("es una prueba de")) + "es una prueba de"
        message_path.write_text(maximum_message, encoding="utf-8")
        verify_search([executable, str(message_path), "7", "3"], 7, maximum_message, 8)
    print("OK: referencia secuencial de archivos, frase, 56 bits, limites y errores.")


def check_mpi_blocks(binary_directory, mpi_launcher):
    """Comprueba la lectura del archivo y el grupo de claves de cada trabajador."""
    executable = str(binary_directory / "aes_search_mpi_blocks")
    message = PROJECT_MESSAGE
    with tempfile.TemporaryDirectory(prefix="aes-mpi-blocks-") as directory:
        message_path = Path(directory) / "mensaje.txt"
        message_path.write_text(message + "\r\n", encoding="utf-8")
        for process_count, unknown_bits, secret_candidate in [
            (1, 12, 4095), (2, 16, 50000), (3, 13, 8191), (4, 1, 1),
        ]:
            verify_search(
                mpi_launcher + ["-np", str(process_count), executable, str(message_path),
                            str(secret_candidate), str(unknown_bits)],
                secret_candidate, message,
            )
        # Probar la clave 0 permite revisar el tamaño inicial de 56 bits sin una búsqueda larga.
        verify_search(mpi_launcher + ["-np", "2", executable, str(message_path), "0"],
                      0, message)
        check_required_project_cases(executable, mpi_launcher, message_path)
        base_command = mpi_launcher + ["-np", "2", executable]
        for arguments in [[], [str(message_path)], [str(message_path), "4", "2"],
                          [str(message_path), "0", "57"],
                          [str(message_path), "18446744073709551616", "8"],
                          [str(message_path.with_name("no_existe.txt")), "0", "8"]]:
            run_program(base_command + arguments, successful=False)
        for invalid_text in ["", "Mensaje sin la frase requerida", "a" * 4097]:
            message_path.write_text(invalid_text, encoding="utf-8")
            run_program(base_command + [str(message_path), "0", "8"], successful=False)
        maximum_message = "x" * (4096 - len("es una prueba de")) + "es una prueba de"
        message_path.write_text(maximum_message, encoding="utf-8")
        verify_search(base_command + [str(message_path), "7", "3"],
                      7, maximum_message, 8)
    print("OK: MPI por bloques, archivos, frase, limite de 4096 bytes y errores.")


def check_mpi_hybrid(binary_directory, mpi_launcher):
    """Prueba k=8,16,32 y comprueba los avisos que permiten detener la búsqueda."""
    executable = str(binary_directory / "aes_search_mpi_hybrid")
    message = PROJECT_MESSAGE
    with tempfile.TemporaryDirectory(prefix="aes-mpi-hybrid-") as directory:
        message_path = Path(directory) / "mensaje.txt"
        message_path.write_text(message + "\r\n", encoding="utf-8")
        for sequential_trials_per_turn in [8, 16, 32]:
            for process_count, unknown_bits, secret_candidate in [
                (1, 12, 4095), (2, 16, 50000), (3, 13, 8191),
                (4, 1, 1), (4, 16, 0), (4, 16, 65535),
            ]:
                output = verify_search(
                    mpi_launcher + ["-np", str(process_count), executable, str(message_path),
                                str(secret_candidate), str(unknown_bits),
                                str(sequential_trials_per_turn), "42"],
                    secret_candidate, message,
                )
                verify_hybrid_statistics(output, unknown_bits, process_count, secret_candidate)
            check_required_project_cases(executable, mpi_launcher, message_path,
                                           sequential_trials_per_turn)
        base_command = mpi_launcher + ["-np", "4", executable]
        # Revisa los valores iniciales y los límites de k y la semilla con búsquedas cortas.
        verify_search(base_command + [str(message_path), "0"], 0, message)
        verify_search(base_command + [str(message_path), "15", "4",
                                      str((1 << 64) - 1), str((1 << 64) - 1)],
                      15, message)
        verify_search(base_command + [str(message_path), "15", "4", "8", "0"],
                      15, message)
        for arguments in [[], [str(message_path), "256", "8"],
                          [str(message_path), "0", "57"],
                          [str(message_path), "0", "8", "0"],
                          [str(message_path), "0", "8", "-1"],
                          [str(message_path), "0", "8", "8", "-1"],
                          [str(message_path), "0", "8", "8", str(1 << 64)],
                          [str(message_path), "0", "8", "8", "1", "extra"]]:
            run_program(base_command + arguments, successful=False)
        for invalid_text in ["", "Sin la frase requerida", "a" * 4097]:
            message_path.write_text(invalid_text, encoding="utf-8")
            run_program(base_command + [str(message_path), "0", "8"], successful=False)
        maximum_message = "x" * (4096 - len("es una prueba de")) + "es una prueba de"
        message_path.write_text(maximum_message, encoding="utf-8")
        verify_search(base_command + [str(message_path), "7", "3", "8"],
                      7, maximum_message)
    for process_count in [1, 2, 3, 4]:
        run_program(mpi_launcher + ["-np", str(process_count),
                               str(binary_directory / "test_hybrid_mpi")])
    print("OK: MPI hibrida k=8/16/32, seeds, archivos, ranks vacios y agotamiento sin hallazgo.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin-dir", type=Path, default=Path("build/bin"))
    mpi_modes = parser.add_mutually_exclusive_group()
    mpi_modes.add_argument("--mpi-blocks", action="store_true")
    mpi_modes.add_argument("--mpi-hybrid", action="store_true")
    parser.add_argument("--mpiexec", default="mpirun")
    command_options = parser.parse_args()
    binary_directory = command_options.bin_dir.resolve()
    if command_options.mpi_hybrid:
        check_mpi_hybrid(binary_directory, shlex.split(command_options.mpiexec))
    elif command_options.mpi_blocks:
        check_mpi_blocks(binary_directory, shlex.split(command_options.mpiexec))
    else:
        check_sequential_file(binary_directory)


main()
