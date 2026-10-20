/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3169 - Computacion Paralela y Distribuida
 * Miembros:    Adriana Palacios
 *              Bryan Martinez
 *              Daniela Ramirez
 * Ejercicio:   Busqueda paralela de una clave AES mediante fuerza bruta
 * Descripcion: varios trabajadores usan MPI para repartirse las posibles
 *              claves. Terminan al encontrar una coincidencia o probarlas todas.
 *
 *              Uso: mpirun -np n ./build/bin/aes_search_mpi
 *                       [bits] [clave_secreta] [mensaje]
 *----------------------------------------------------------------------*/

#include "aes_ctr.h"
#include "search_reporting.h"

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <openssl/err.h>

/* Pruebas que hace cada trabajador antes de avisar si encontró una coincidencia. */
#define MPI_CANDIDATES_PER_ROUND UINT64_C(4096)

/* Detener a todos evita que otros trabajadores sigan esperando después de un error. */
_Noreturn static void abort_search(const char *description, int process_rank)
{
    fprintf(stderr, "Proceso %d: %s\n", process_rank, description);
    ERR_print_errors_fp(stderr);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    exit(EXIT_FAILURE);
}

static void share_problem(AesSearchProblem *search_problem, const SearchOptions *search_options)
{
    MPI_Bcast(search_problem->base_key, AES_KEY_LENGTH, MPI_UNSIGNED_CHAR,
              0, MPI_COMM_WORLD);
    MPI_Bcast(search_problem->initialization_vector, AES_IV_LENGTH, MPI_UNSIGNED_CHAR,
              0, MPI_COMM_WORLD);
    MPI_Bcast(search_problem->ciphertext, (int)search_options->message_length, MPI_UNSIGNED_CHAR,
              0, MPI_COMM_WORLD);
}

/*
 * Los trabajadores se reparten las claves por turnos. Con dos, uno prueba 0, 2,
 * 4... y el otro 1, 3, 5... Todos revisan juntos el resultado de cada ronda,
 * incluso quienes ya no tienen claves, para que nadie quede esperando.
 */
static SearchResult search_distributed(AesCtrContext *aes_context,
                                       const SearchOptions *search_options,
                                       const AesSearchProblem *search_problem,
                                       int process_rank, int process_count)
{
    uint64_t local_found_candidate = AES_CANDIDATE_NOT_FOUND;
    uint64_t local_candidates_tested = 0;
    uint64_t candidates_per_round = MPI_CANDIDATES_PER_ROUND * (uint64_t)process_count;
    SearchResult search_result = {AES_CANDIDATE_NOT_FOUND, 0, 0.0};

    MPI_Barrier(MPI_COMM_WORLD);
    double start_seconds = MPI_Wtime();
    for (uint64_t round_first_candidate = 0; round_first_candidate < search_options->total_candidates;
         round_first_candidate += candidates_per_round) {
        uint64_t round_candidate_limit = round_first_candidate + candidates_per_round;
        if (round_candidate_limit > search_options->total_candidates) {
            round_candidate_limit = search_options->total_candidates;
        }
        for (uint64_t candidate = round_first_candidate + (uint64_t)process_rank;
             candidate < round_candidate_limit; candidate += (uint64_t)process_count) {
            int matches_prefix;
            if (!aes_ctr_test_candidate(aes_context, search_options, search_problem, candidate,
                                         &matches_prefix)) {
                abort_search("Error al verificar una candidata.", process_rank);
            }
            local_candidates_tested++;
            if (matches_prefix) {
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
    /* Cada trabajador mide su propia duración; no se comparan las horas de sus relojes. */
    MPI_Reduce(&local_elapsed_seconds, &search_result.elapsed_seconds, 1,
               MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_candidates_tested, &search_result.candidates_tested, 1,
               MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
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

    SearchOptions search_options;
    int local_arguments_valid = search_options_parse(argument_count, arguments, &search_options,
                                           process_rank == 0 ? stderr : NULL);
    int all_arguments_valid;
    /* Si un trabajador recibe datos inválidos, todos terminan juntos. */
    MPI_Allreduce(&local_arguments_valid, &all_arguments_valid, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    if (!all_arguments_valid) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    AesCtrContext aes_context = {0};
    AesSearchProblem search_problem = {0};
    if (!aes_ctr_context_open(&aes_context)) {
        abort_search("No se pudo preparar OpenSSL.", process_rank);
    }
    if (process_rank == 0 && !aes_ctr_prepare_problem(&aes_context, &search_options, &search_problem)) {
        abort_search("No se pudo preparar el mensaje cifrado.", process_rank);
    }
    share_problem(&search_problem, &search_options);
    SearchResult search_result = search_distributed(&aes_context, &search_options, &search_problem,
                                             process_rank, process_count);
    if (process_rank == 0 &&
        !search_report(&aes_context, &search_options, &search_problem, &search_result, 1, process_count)) {
        abort_search("No se pudo descifrar el resultado.", process_rank);
    }
    aes_ctr_context_close(&aes_context);
    MPI_Finalize();
    return EXIT_SUCCESS;
}
