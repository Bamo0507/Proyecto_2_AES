/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3169 - Computacion Paralela y Distribuida
 * Miembros:    Adriana Palacios
 *              Bryan Martinez
 *              Daniela Ramirez
 * Ejercicio:   Busqueda paralela de una clave AES mediante fuerza bruta
 * Descripcion: los procesos MPI se reparten las claves candidatas y
 *              se detienen juntos al encontrar la clave o agotar el rango.
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

/* Candidatas por proceso antes de comunicar una posible coincidencia. */
#define MPI_CANDIDATES_PER_ROUND UINT64_C(4096)

/* Un error local no debe dejar a los otros procesos esperando una colectiva. */
_Noreturn static void abort_search(const char *description, int process_rank)
{
    fprintf(stderr, "Proceso %d: %s\n", process_rank, description);
    ERR_print_errors_fp(stderr);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    exit(EXIT_FAILURE);
}

static void share_problem(AesSearchProblem *problem, const SearchOptions *options)
{
    MPI_Bcast(problem->base_key, AES_KEY_LENGTH, MPI_UNSIGNED_CHAR,
              0, MPI_COMM_WORLD);
    MPI_Bcast(problem->initialization_vector, AES_IV_LENGTH, MPI_UNSIGNED_CHAR,
              0, MPI_COMM_WORLD);
    MPI_Bcast(problem->ciphertext, (int)options->message_length, MPI_UNSIGNED_CHAR,
              0, MPI_COMM_WORLD);
}

/*
 * Distribucion ciclica: rank r prueba c con c % process_count == r.
 * Todos ejecutan las mismas rondas, incluso si su rango local esta vacio,
 * para llegar a MPI_Allreduce en el mismo orden y evitar bloqueos.
 */
static SearchResult search_distributed(AesCtrContext *context,
                                       const SearchOptions *options,
                                       const AesSearchProblem *problem,
                                       int process_rank, int process_count)
{
    uint64_t local_found_candidate = AES_CANDIDATE_NOT_FOUND;
    uint64_t local_candidates_tested = 0;
    uint64_t round_size = MPI_CANDIDATES_PER_ROUND * (uint64_t)process_count;
    SearchResult result = {AES_CANDIDATE_NOT_FOUND, 0, 0.0};

    MPI_Barrier(MPI_COMM_WORLD);
    double start_seconds = MPI_Wtime();
    for (uint64_t round_start = 0; round_start < options->total_candidates;
         round_start += round_size) {
        uint64_t round_end = round_start + round_size;
        if (round_end > options->total_candidates) {
            round_end = options->total_candidates;
        }
        for (uint64_t candidate = round_start + (uint64_t)process_rank;
             candidate < round_end; candidate += (uint64_t)process_count) {
            int matches_prefix;
            if (!aes_ctr_test_candidate(context, options, problem, candidate,
                                         &matches_prefix)) {
                abort_search("Error al verificar una candidata.", process_rank);
            }
            local_candidates_tested++;
            if (matches_prefix) {
                local_found_candidate = candidate;
                break;
            }
        }
        MPI_Allreduce(&local_found_candidate, &result.found_candidate, 1,
                      MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);
        if (result.found_candidate != AES_CANDIDATE_NOT_FOUND) {
            break;
        }
    }

    double local_elapsed_seconds = MPI_Wtime() - start_seconds;
    /* Duraciones locales: no se restan timestamps de procesos diferentes. */
    MPI_Reduce(&local_elapsed_seconds, &result.elapsed_seconds, 1,
               MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_candidates_tested, &result.candidates_tested, 1,
               MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
    return result;
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

    SearchOptions options;
    int local_valid = search_options_parse(argument_count, arguments, &options,
                                           process_rank == 0 ? stderr : NULL);
    int all_valid;
    /* Si algun rank rechaza sus argumentos, todos finalizan juntos. */
    MPI_Allreduce(&local_valid, &all_valid, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
    if (!all_valid) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    AesCtrContext context = {0};
    AesSearchProblem problem = {0};
    if (!aes_ctr_context_open(&context)) {
        abort_search("No se pudo preparar OpenSSL.", process_rank);
    }
    if (process_rank == 0 && !aes_ctr_prepare_problem(&context, &options, &problem)) {
        abort_search("No se pudo preparar el mensaje cifrado.", process_rank);
    }
    share_problem(&problem, &options);
    SearchResult result = search_distributed(&context, &options, &problem,
                                             process_rank, process_count);
    if (process_rank == 0 &&
        !search_report(&context, &options, &problem, &result, 1, process_count)) {
        abort_search("No se pudo descifrar el resultado.", process_rank);
    }
    aes_ctr_context_close(&context);
    MPI_Finalize();
    return EXIT_SUCCESS;
}
