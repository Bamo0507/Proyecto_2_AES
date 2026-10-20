/*
 * Muestra los resultados después de que el programa termine de buscar. Recupera
 * el mensaje con la clave elegida y presenta el tiempo y la cantidad de
 * pruebas.
 */
#include "search_reporting.h"

#include <inttypes.h>
#include <stdio.h>

/*
 * Muestra cada byte con dos símbolos del sistema hexadecimal, que usa 0-9 y
 * a-f. Así se puede leer una clave completa, incluidos sus ceros iniciales.
 */
static void print_hexadecimal(const unsigned char *bytes, size_t byte_count)
{
    for (size_t byte_index = 0; byte_index < byte_count; byte_index++) {
        printf("%02x", (unsigned int)bytes[byte_index]);
    }
    putchar('\n');
}

int search_report(AesCtrContext *aes_context, const SearchOptions *search_options,
                   const AesSearchProblem *search_problem, const SearchResult *search_result,
                   int is_distributed_search, int process_count)
{
    unsigned char found_key[AES_KEY_LENGTH];
    unsigned char decrypted_message[AES_OUTPUT_CAPACITY];
    /*
     * Primero descifra para no mostrar un resultado incompleto si el cifrado
     * falla. Este paso queda fuera del tiempo de búsqueda.
     */
    if (search_result->found_candidate != AES_CANDIDATE_NOT_FOUND) {
        aes_ctr_make_candidate_key(search_problem->base_key, search_result->found_candidate,
                                   search_options->unknown_key_bits, found_key);
        if (!aes_ctr_transform(aes_context, found_key, search_problem->initialization_vector,
                                search_problem->ciphertext, search_options->message_length,
                                decrypted_message)) {
            return 0;
        }
    }

    if (is_distributed_search) {
        printf("Ejecucion: paralela con %d procesos\n", process_count);
    } else {
        printf("Ejecucion: secuencial\n");
    }
    printf("Rango explorado: 2^%d candidatas\n", search_options->unknown_key_bits);
    if (search_result->found_candidate != AES_CANDIDATE_NOT_FOUND) {
        printf("Clave encontrada: candidata %" PRIu64, search_result->found_candidate);
        if (is_distributed_search) {
            /* Este cálculo supone que los trabajadores se repartieron las claves por turnos. */
            printf(" (proceso %" PRIu64 ")",
                   search_result->found_candidate % (uint64_t)process_count);
        }
        putchar('\n');
        printf("Clave AES-128: ");
        print_hexadecimal(found_key, AES_KEY_LENGTH);
        printf("Mensaje: ");
        /* Imprime solo los bytes del mensaje; OpenSSL no agrega una marca final de texto. */
        fwrite(decrypted_message, 1, search_options->message_length, stdout);
        putchar('\n');
    } else {
        printf("No se encontro la clave.\n");
    }
    printf("Candidatas probadas: %" PRIu64 "\n", search_result->candidates_tested);
    printf("Tiempo: %.9f segundos\n", search_result->elapsed_seconds);
    /* Si el reloj no alcanzó a medir una duración, se omite la división. */
    if (search_result->elapsed_seconds > 0.0) {
        printf("Rendimiento: %.0f candidatas/segundo\n",
               (double)search_result->candidates_tested / search_result->elapsed_seconds);
    }
    return 1;
}
