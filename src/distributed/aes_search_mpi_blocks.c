/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3169 - Computacion Paralela y Distribuida
 * Miembros:    Adriana Palacios
 *              Bryan Martinez
 *              Daniela Ramirez
 * Ejercicio:   Busqueda paralela de una clave AES mediante fuerza bruta
 * Descripcion: lee un texto desde un archivo, lo cifra y descifra con
 *              AES-128 usando la clave indicada. Luego los procesos MPI
 *              prueban posibles claves, cada uno en su grupo de numeros
 *              consecutivos, hasta recuperar la frase que se busca.
 *
 *              Variante por bloques de los ejercicios 1 y 2.
 *              Uso: mpirun -np n ./build/bin/aes_search_mpi_blocks
 *                       <archivo.txt> <clave> [bits]
 *----------------------------------------------------------------------*/

#include "file_search_mpi.h"

#include <stdlib.h>

#define BLOCK_CANDIDATES_PER_ROUND UINT64_C(4096)

/* Cada trabajador busca en su grupo y revisa con los demás el resultado cada 4096 pruebas. */
static FileSearchResult search_blocks(AesCtrContext *aes_context,
                                      const FileSearchOptions *search_options,
                                      const FileSearchProblem *search_problem,
                                      int process_rank, int process_count)
{
    uint64_t first_candidate;
    uint64_t candidate_limit;
    file_search_block_bounds(search_options->total_candidates, process_count, process_rank,
                               &first_candidate, &candidate_limit);
    uint64_t block_candidate_count = (search_options->total_candidates + (uint64_t)process_count - 1) /
                          (uint64_t)process_count;
    uint64_t local_found_candidate = AES_CANDIDATE_NOT_FOUND;
    uint64_t local_candidates_tested = 0;
    FileSearchResult search_result = {AES_CANDIDATE_NOT_FOUND, 0, 0.0, -1};
    MPI_Barrier(MPI_COMM_WORLD);
    double start_seconds = MPI_Wtime();
    /*
 * * Todos revisan las mismas rondas, incluidos quienes ya no tienen claves, *
 * para evitar esperas sin fin.
 */
    for (uint64_t candidate_position = 0; candidate_position < block_candidate_count;
         candidate_position += BLOCK_CANDIDATES_PER_ROUND) {
        uint64_t round_first_candidate = first_candidate + candidate_position;
        uint64_t round_candidate_limit = round_first_candidate + BLOCK_CANDIDATES_PER_ROUND;
        if (round_first_candidate > candidate_limit) {
            round_first_candidate = candidate_limit;
        }
        if (round_candidate_limit > candidate_limit) {
            round_candidate_limit = candidate_limit;
        }
        for (uint64_t candidate = round_first_candidate; candidate < round_candidate_limit; candidate++) {
            int matches_phrase;
            if (!file_search_test_candidate(aes_context, search_problem, candidate, &matches_phrase)) {
                mpi_file_search_abort("Error al verificar una candidata por bloques.", process_rank);
            }
            local_candidates_tested++;
            if (matches_phrase) {
                local_found_candidate = candidate;
                break;
            }
        }
        MPI_Allreduce(&local_found_candidate, &search_result.found_candidate, 1,
                      MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);
        if (search_result.found_candidate != AES_CANDIDATE_NOT_FOUND) {
            break;
        }
    }
    double local_elapsed_seconds = MPI_Wtime() - start_seconds;
    MPI_Reduce(&local_elapsed_seconds, &search_result.elapsed_seconds, 1,
               MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_candidates_tested, &search_result.candidates_tested, 1,
               MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
    if (search_result.found_candidate != AES_CANDIDATE_NOT_FOUND) {
        search_result.winning_rank = (int)(search_result.found_candidate / block_candidate_count);
    }
    return search_result;
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
    int local_arguments_valid = file_search_parse_options(argument_count, arguments, &search_options,
                                                 process_rank == 0 ? stderr : NULL);
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
    FileSearchResult search_result = search_blocks(&aes_context, &search_options, &search_problem,
                                             process_rank, process_count);
    if (process_rank == 0 &&
        !file_search_report(&aes_context, &search_options, &search_problem,
                              &search_result, "bloques", process_count)) {
        mpi_file_search_abort("No se pudo descifrar el resultado por bloques.", process_rank);
    }
    aes_ctr_context_close(&aes_context);
    MPI_Finalize();
    return EXIT_SUCCESS;
}
