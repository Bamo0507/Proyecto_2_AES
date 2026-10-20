#!/bin/sh
# Registra salidas originales para analizarlas despues. Uso: sh scripts/run_benchmarks.sh
set -eu
cd "$(dirname "$0")/.."

benchmark_bits=${BITS:-20}
benchmark_repetitions=${REPETITIONS:-5}
benchmark_process_counts=${PROCESS_COUNTS:-"1 2 4"}
benchmark_launcher=${MPIEXEC:-mpirun}
benchmark_compiler=${CC:-cc}
benchmark_mpi_compiler=${MPICC:-mpicc}

require_positive_integer() {
    case "$2" in
        ''|*[!0-9]*) echo "$1 debe ser un entero positivo." >&2; exit 1 ;;
    esac
    if [ "${#2}" -gt 10 ]; then
        echo "$1 esta fuera del rango permitido." >&2
        exit 1
    fi
    if [ "$2" -lt 1 ] || [ "$2" -gt 2147483647 ]; then
        echo "$1 esta fuera del rango permitido." >&2
        exit 1
    fi
}

require_positive_integer BITS "$benchmark_bits"
if [ "$benchmark_bits" -gt 40 ]; then
    echo "BITS debe estar entre 1 y 40." >&2
    exit 1
fi
require_positive_integer REPETITIONS "$benchmark_repetitions"
if [ -z "$benchmark_process_counts" ]; then
    echo "PROCESS_COUNTS debe contener al menos un numero de procesos." >&2
    exit 1
fi
for process_count in $benchmark_process_counts; do
    require_positive_integer PROCESS_COUNTS "$process_count"
done

# Carpeta propia para conservar los flags y binarios de este lote.
run_directory="results/runs/$(date -u +%Y%m%dT%H%M%SZ)-$$"
mkdir -p "$run_directory"
benchmark_build_directory="$run_directory/build"
make BUILD_DIR="$benchmark_build_directory" all > "$run_directory/build.log" 2>&1 || {
    cat "$run_directory/build.log" >&2
    exit 1
}
{
    date -u
    uname -a
    echo "BITS=$benchmark_bits"
    echo "SECRET_CANDIDATE=2^BITS-1"
    echo "MESSAGE=Puedes lograrlo!"
    echo "REPETITIONS=$benchmark_repetitions"
    echo "PROCESS_COUNTS=$benchmark_process_counts"
    echo "MPIEXEC=$benchmark_launcher"
    echo "Commit de referencia (los cambios locales pueden diferir):"
    git rev-parse HEAD
    git status --short
    make -s BUILD_DIR="$benchmark_build_directory" print-config
    echo "Checksums de binarios (cksum):"
    cksum "$benchmark_build_directory"/bin/aes_search_*
    echo "Version del compilador C:"
    "$benchmark_compiler" --version
    echo "Version del compilador usado por el wrapper MPI:"
    "$benchmark_mpi_compiler" --version
    echo "Version del launcher MPI:"
    "$benchmark_launcher" --version
} > "$run_directory/metadata.txt" 2>&1

# Incluye los fuentes usados, incluso archivos nuevos aun no versionados.
tar -czf "$run_directory/source.tar.gz" Makefile src scripts docs/coding_conventions.md docs/benchmark_methodology.md

# Calentamiento no incluido entre las repeticiones registradas.
"$benchmark_build_directory/bin/aes_search_ctr" "$benchmark_bits" > "$run_directory/warmup_sequential.log" 2>&1
for process_count in $benchmark_process_counts; do
    "$benchmark_launcher" -np "$process_count" "$benchmark_build_directory/bin/aes_search_mpi" "$benchmark_bits" > "$run_directory/warmup_mpi_${process_count}.log" 2>&1
done

repetition=1
while [ "$repetition" -le "$benchmark_repetitions" ]; do
    "$benchmark_build_directory/bin/aes_search_ctr" "$benchmark_bits" > "$run_directory/sequential_${repetition}.log" 2>&1
    for process_count in $benchmark_process_counts; do
        "$benchmark_launcher" -np "$process_count" "$benchmark_build_directory/bin/aes_search_mpi" "$benchmark_bits" > "$run_directory/mpi_${process_count}_${repetition}.log" 2>&1
    done
    repetition=$((repetition + 1))
done
echo "Ejecuciones guardadas en $run_directory"
