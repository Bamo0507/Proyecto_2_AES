/*
 * Busca la clave de un mensaje leído desde un archivo, con un solo trabajador.
 * Comparte el cifrado y la búsqueda de la frase con las versiones MPI para
 * que sus tiempos puedan compararse.
 * Uso: ./build/bin/aes_search_file <archivo.txt> <clave> [bits]
 */
#include "file_search.h"
#include "search_timing.h"

#include <stdlib.h>
#include <openssl/err.h>

static int search_file_sequential(AesCtrContext *aes_context,
                                   const FileSearchOptions *search_options,
                                   const FileSearchProblem *search_problem,
                                   FileSearchResult *search_result)
{
    double start_seconds;
    double finish_seconds;
    if (!search_monotonic_seconds(&start_seconds)) {
        return 0;
    }
    for (uint64_t candidate = 0; candidate < search_options->total_candidates; candidate++) {
        int matches_phrase;
        if (!file_search_test_candidate(aes_context, search_problem, candidate, &matches_phrase)) {
            return 0;
        }
        search_result->candidates_tested++;
        if (matches_phrase) {
            search_result->found_candidate = candidate;
            search_result->winning_rank = 0;
            break;
        }
    }
    if (!search_monotonic_seconds(&finish_seconds)) {
        return 0;
    }
    search_result->elapsed_seconds = finish_seconds - start_seconds;
    return 1;
}

int main(int argument_count, char *arguments[])
{
    FileSearchOptions search_options;
    if (!file_search_parse_options(argument_count, arguments, &search_options, stderr)) {
        return EXIT_FAILURE;
    }

    AesCtrContext aes_context = {0};
    FileSearchProblem search_problem = {0};
    FileSearchResult search_result = {AES_CANDIDATE_NOT_FOUND, 0, 0.0, -1};
    int exit_status = EXIT_FAILURE;
    if (!aes_ctr_context_open(&aes_context)) {
        fprintf(stderr, "No se pudo preparar OpenSSL.\n");
        goto cleanup;
    }
    if (!file_search_prepare_problem(&aes_context, &search_options, &search_problem)) {
        fprintf(stderr, "No se pudo preparar el mensaje cifrado.\n");
        goto cleanup;
    }
    if (!search_file_sequential(&aes_context, &search_options, &search_problem, &search_result) ||
        !file_search_report(&aes_context, &search_options, &search_problem,
                             &search_result, "secuencial", 0, 1)) {
        fprintf(stderr, "No se pudo completar la busqueda secuencial del archivo.\n");
        goto cleanup;
    }
    exit_status = EXIT_SUCCESS;

cleanup:
    if (exit_status != EXIT_SUCCESS) {
        ERR_print_errors_fp(stderr);
    }
    aes_ctr_context_close(&aes_context);
    return exit_status;
}
