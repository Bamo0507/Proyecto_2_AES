/**
 * @file search_reporting.c
 * @brief Presenta claves, texto y estadisticas con un formato comun.
 *
 * No ejecuta la busqueda ni calcula reducciones MPI. Recibe el resultado final
 * del ejecutable, descifra el mensaje si hay candidata y escribe en stdout.
 * Los contratos y el significado de cada estadistica estan en search_reporting.h.
 */
#include "search_reporting.h"

#include <inttypes.h>
#include <stdio.h>

/*
 * Escribe byte_count bytes legibles como dos digitos hexadecimales por byte,
 * incluidos los ceros iniciales, y agrega un salto de linea. No modifica bytes.
 */
static void print_hexadecimal(const unsigned char *bytes, size_t byte_count)
{
    for (size_t byte_index = 0; byte_index < byte_count; byte_index++) {
        printf("%02x", (unsigned int)bytes[byte_index]);
    }
    putchar('\n');
}

int search_report(AesCtrContext *context, const SearchOptions *options,
                   const AesSearchProblem *problem, const SearchResult *result,
                   int distributed, int process_count)
{
    unsigned char found_key[AES_KEY_LENGTH];
    unsigned char plaintext[AES_OUTPUT_CAPACITY];
    /* Se descifra antes de imprimir para no producir un reporte parcial si EVP
     * falla. Este trabajo queda fuera de la medicion tomada por el ejecutable. */
    if (result->found_candidate != AES_CANDIDATE_NOT_FOUND) {
        aes_ctr_make_candidate_key(problem->base_key, result->found_candidate,
                                   options->unknown_key_bits, found_key);
        if (!aes_ctr_transform(context, found_key, problem->initialization_vector,
                                problem->ciphertext, options->message_length,
                                plaintext)) {
            return 0;
        }
    }

    if (distributed) {
        printf("Ejecucion: paralela con %d procesos\n", process_count);
    } else {
        printf("Ejecucion: secuencial\n");
    }
    printf("Rango explorado: 2^%d candidatas\n", options->unknown_key_bits);
    if (result->found_candidate != AES_CANDIDATE_NOT_FOUND) {
        printf("Clave encontrada: candidata %" PRIu64, result->found_candidate);
        if (distributed) {
            /* Este propietario corresponde exclusivamente al reparto ciclico. */
            printf(" (proceso %" PRIu64 ")",
                   result->found_candidate % (uint64_t)process_count);
        }
        putchar('\n');
        printf("Clave AES-128: ");
        print_hexadecimal(found_key, AES_KEY_LENGTH);
        printf("Mensaje: ");
        /* EVP produce bytes sin terminador; fwrite evita leer fuera del mensaje. */
        fwrite(plaintext, 1, options->message_length, stdout);
        putchar('\n');
    } else {
        printf("No se encontro la clave.\n");
    }
    printf("Candidatas probadas: %" PRIu64 "\n", result->candidates_tested);
    printf("Tiempo: %.9f segundos\n", result->elapsed_seconds);
    /* Evita dividir entre cero en ejecuciones por debajo de la resolucion medida. */
    if (result->elapsed_seconds > 0.0) {
        printf("Rendimiento: %.0f candidatas/segundo\n",
               (double)result->candidates_tested / result->elapsed_seconds);
    }
    return 1;
}
