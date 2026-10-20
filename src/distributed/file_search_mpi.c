/*
 * Comparte el tamaño del mensaje, el texto cifrado y el dato de inicio del
 * cifrado. Los trabajadores usan esos datos para probar claves sin recibir la
 * respuesta correcta.
 */
#include "file_search_mpi.h"

#include <stdlib.h>
#include <openssl/err.h>

_Noreturn void mpi_file_search_abort(const char *description, int process_rank)
{
    fprintf(stderr, "Proceso %d: %s\n", process_rank, description);
    ERR_print_errors_fp(stderr);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    exit(EXIT_FAILURE);
}

int mpi_file_search_prepare(AesCtrContext *aes_context, const FileSearchOptions *search_options,
                            FileSearchProblem *search_problem, int process_rank)
{
    if (!aes_ctr_context_open(aes_context)) {
        mpi_file_search_abort("No se pudo preparar OpenSSL.", process_rank);
    }
    int problem_ready = 1;
    if (process_rank == 0) {
        problem_ready = file_search_prepare_problem(aes_context, search_options, search_problem);
        if (!problem_ready) {
            ERR_print_errors_fp(stderr);
        }
    }
    MPI_Bcast(&problem_ready, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!problem_ready) {
        return 0;
    }
    int message_length = (int)search_problem->message_length;
    MPI_Bcast(&message_length, 1, MPI_INT, 0, MPI_COMM_WORLD);
    search_problem->message_length = (size_t)message_length;
    MPI_Bcast(search_problem->initialization_vector, AES_IV_LENGTH, MPI_UNSIGNED_CHAR,
              0, MPI_COMM_WORLD);
    MPI_Bcast(search_problem->ciphertext, message_length, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    return 1;
}
