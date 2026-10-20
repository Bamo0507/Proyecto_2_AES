/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3169 - Computacion Paralela y Distribuida
 * Miembros:    Adriana Palacios
 *              Bryan Martinez
 *              Daniela Ramirez
 * Ejercicio:   Busqueda secuencial de una clave AES mediante fuerza bruta
 * Descripcion: un unico proceso prueba claves candidatas en orden
 *              y verifica cada una con el prefijo conocido del mensaje.
 *
 *              Uso: ./build/bin/aes_search_ctr
 *                       [bits] [clave_secreta] [mensaje]
 *----------------------------------------------------------------------*/

#define _POSIX_C_SOURCE 200809L

#include "aes_ctr.h"
#include "search_reporting.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <openssl/err.h>

/* Devuelve 0 si falla el reloj; el llamador conserva el control del cleanup. */
static int monotonic_seconds(double *seconds)
{
    struct timespec current_time;
    if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0) {
        perror("Error al consultar el reloj");
        return 0;
    }
    *seconds = (double)current_time.tv_sec +
               (double)current_time.tv_nsec / 1000000000.0;
    return 1;
}

/* Solo cambia la candidata: el problema y el contexto se preparan antes. */
static int search_sequential(AesCtrContext *context, const SearchOptions *options,
                             const AesSearchProblem *problem, SearchResult *result)
{
    double start_seconds;
    double finish_seconds;
    if (!monotonic_seconds(&start_seconds)) {
        return 0;
    }
    for (uint64_t candidate = 0; candidate < options->total_candidates; candidate++) {
        int matches_prefix;
        if (!aes_ctr_test_candidate(context, options, problem, candidate,
                                     &matches_prefix)) {
            return 0;
        }
        result->candidates_tested++;
        if (matches_prefix) {
            result->found_candidate = candidate;
            break;
        }
    }
    if (!monotonic_seconds(&finish_seconds)) {
        return 0;
    }
    result->elapsed_seconds = finish_seconds - start_seconds;
    return 1;
}

int main(int argument_count, char *arguments[])
{
    SearchOptions options;
    if (!search_options_parse(argument_count, arguments, &options, stderr)) {
        return EXIT_FAILURE;
    }

    AesCtrContext context = {0};
    AesSearchProblem problem = {0};
    SearchResult result = {AES_CANDIDATE_NOT_FOUND, 0, 0.0};
    int exit_status = EXIT_FAILURE;
    if (!aes_ctr_context_open(&context)) {
        fprintf(stderr, "No se pudo preparar OpenSSL.\n");
        goto cleanup;
    }
    if (!aes_ctr_prepare_problem(&context, &options, &problem)) {
        fprintf(stderr, "No se pudo preparar el mensaje cifrado.\n");
        goto cleanup;
    }
    /* La preparacion y el reporte quedan fuera del intervalo medido. */
    if (!search_sequential(&context, &options, &problem, &result) ||
        !search_report(&context, &options, &problem, &result, 0, 1)) {
        fprintf(stderr, "No se pudo completar la busqueda secuencial.\n");
        goto cleanup;
    }
    exit_status = EXIT_SUCCESS;

cleanup:
    if (exit_status != EXIT_SUCCESS) {
        ERR_print_errors_fp(stderr);
    }
    aes_ctr_context_close(&context);
    return exit_status;
}
