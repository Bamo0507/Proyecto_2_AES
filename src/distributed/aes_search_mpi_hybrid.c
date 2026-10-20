/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso: CC3169 - Computacion Paralela y Distribuida
 * Miembros: Adriana Palacios, Bryan Martinez, Daniela Ramirez
 * Ejercicio: Inciso 3, busqueda que combina dos formas de probar las claves.
 * Uso: mpirun -np 4 ./build/bin/aes_search_mpi_hybrid
 *          <archivo.txt> <clave> [bits] [k] [semilla]
 * Ejemplo: mpirun -np 4 ./build/bin/aes_search_mpi_hybrid mensaje.txt 12345 16 16 1
 * Cada trabajador prueba k claves en orden y luego hace un tiro revuelto,
 * sin repetir claves que ya haya probado.
 *----------------------------------------------------------------------*/
#include "hybrid_search_mpi.h"

#include <inttypes.h>
#include <stdlib.h>

static int parse_hybrid_options(int argument_count, char *arguments[],
                                FileSearchOptions *search_options, uint64_t *sequential_trials_per_turn,
                                uint64_t *seed, int process_rank)
{
    if (argument_count < 3 || argument_count > 6) {
        if (process_rank == 0) {
            fprintf(stderr, "Uso: %s <archivo.txt> <clave> [bits] [k] [semilla]\n",
                    arguments[0]);
        }
        return 0;
    }
    if (!file_search_parse_options(argument_count > 4 ? 4 : argument_count, arguments,
                                    search_options, process_rank == 0 ? stderr : NULL)) {
        return 0;
    }
    *sequential_trials_per_turn = HYBRID_DEFAULT_SEQUENTIAL_TRIALS;
    *seed = HYBRID_DEFAULT_SEED;
    if ((argument_count > 4 &&
         !search_parse_unsigned_decimal(arguments[4], sequential_trials_per_turn)) ||
        *sequential_trials_per_turn == 0 ||
        (argument_count > 5 && !search_parse_unsigned_decimal(arguments[5], seed))) {
        if (process_rank == 0) {
            fprintf(stderr, "k debe ser positivo y semilla un uint64_t decimal.\n");
        }
        return 0;
    }
    return 1;
}

int main(int argument_count, char *arguments[])
{
    if (MPI_Init(&argument_count, &arguments) != MPI_SUCCESS) {
        return EXIT_FAILURE;
    }
    int process_rank;
    int process_count;
    MPI_Comm_rank(MPI_COMM_WORLD, &process_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &process_count);
    FileSearchOptions search_options;
    uint64_t sequential_trials_per_turn;
    uint64_t seed;
    int local_arguments_valid = parse_hybrid_options(argument_count, arguments, &search_options,
                                            &sequential_trials_per_turn, &seed, process_rank);
    int all_arguments_valid;
    MPI_Allreduce(&local_arguments_valid, &all_arguments_valid, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    if (!all_arguments_valid) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }
    AesCtrContext aes_context = {0};
    FileSearchProblem search_problem = {0};
    if (!mpi_file_search_prepare(&aes_context, &search_options, &search_problem, process_rank)) {
        aes_ctr_context_close(&aes_context);
        MPI_Finalize();
        return EXIT_FAILURE;
    }
    HybridSearchResult search_result = mpi_hybrid_search(&aes_context, &search_options, &search_problem,
                                                   sequential_trials_per_turn, seed,
                                                   process_rank, process_count);
    if (process_rank == 0) {
        if (!file_search_report(&aes_context, &search_options, &search_problem, &search_result.summary,
                                  "hibrida: secuencial + permutacion", 1, process_count)) {
            mpi_file_search_abort("No se pudo descifrar el resultado hibrido.", process_rank);
        }
        printf("k: %" PRIu64 "\nSemilla: %" PRIu64 "\n", sequential_trials_per_turn, seed);
        printf("Candidatas secuenciales: %" PRIu64 "\n", search_result.sequential_candidates);
        printf("Candidatas permutadas: %" PRIu64 "\n", search_result.permuted_candidates);
    }
    aes_ctr_context_close(&aes_context);
    MPI_Finalize();
    return EXIT_SUCCESS;
}
