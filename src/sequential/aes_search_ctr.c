/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3169 - Computacion Paralela y Distribuida
 * Miembros:    Adriana Palacios
 *              Bryan Martinez
 *              Daniela Ramirez
 * Ejercicio:   Busqueda secuencial de una clave AES mediante fuerza bruta
 * Descripcion: un trabajador prueba posibles claves una por una
 *              y compara el comienzo del mensaje que logra recuperar.
 *
 *              Uso: ./build/bin/aes_search_ctr
 *                       [bits] [clave_secreta] [mensaje]
 *----------------------------------------------------------------------*/

#include "aes_ctr.h"
#include "search_reporting.h"
#include "search_timing.h"

#include <stdio.h>
#include <stdlib.h>
#include <openssl/err.h>

/* Prueba una clave tras otra y mide la búsqueda sobre el mensaje ya preparado. */
static int search_sequential(AesCtrContext *aes_context, const SearchOptions *search_options,
                             const AesSearchProblem *search_problem, SearchResult *search_result)
{
    double start_seconds;
    double finish_seconds;
    if (!search_monotonic_seconds(&start_seconds)) {
        return 0;
    }
    for (uint64_t candidate = 0; candidate < search_options->total_candidates; candidate++) {
        int matches_prefix;
        if (!aes_ctr_test_candidate(aes_context, search_options, search_problem, candidate,
                                     &matches_prefix)) {
            return 0;
        }
        search_result->candidates_tested++;
        if (matches_prefix) {
            search_result->found_candidate = candidate;
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
    SearchOptions search_options;
    if (!search_options_parse(argument_count, arguments, &search_options, stderr)) {
        return EXIT_FAILURE;
    }

    AesCtrContext aes_context = {0};
    AesSearchProblem search_problem = {0};
    SearchResult search_result = {AES_CANDIDATE_NOT_FOUND, 0, 0.0};
    int exit_status = EXIT_FAILURE;
    if (!aes_ctr_context_open(&aes_context)) {
        fprintf(stderr, "No se pudo preparar OpenSSL.\n");
        goto cleanup;
    }
    if (!aes_ctr_prepare_problem(&aes_context, &search_options, &search_problem)) {
        fprintf(stderr, "No se pudo preparar el mensaje cifrado.\n");
        goto cleanup;
    }
    /* El tiempo cuenta la búsqueda; preparar el mensaje y mostrar resultados se hace aparte. */
    if (!search_sequential(&aes_context, &search_options, &search_problem, &search_result) ||
        !search_report(&aes_context, &search_options, &search_problem, &search_result, 0, 1)) {
        fprintf(stderr, "No se pudo completar la busqueda secuencial.\n");
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
