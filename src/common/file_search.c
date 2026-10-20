/*
 * Lee el mensaje del archivo y comparte las operaciones de cifrado y revisión
 * de claves entre las versiones secuencial, por bloques e híbrida.
 */
#include "file_search.h"

#include <inttypes.h>
#include <string.h>
#include <openssl/rand.h>

int file_search_parse_options(int argument_count, char *arguments[],
                              FileSearchOptions *search_options, FILE *error_stream)
{
    uint64_t unknown_bits = FILE_SEARCH_DEFAULT_BITS;
    if (argument_count < 3 || argument_count > 4) {
        if (error_stream != NULL) {
            fprintf(error_stream, "Uso: %s <archivo.txt> <clave> [bits]\n", arguments[0]);
        }
        return 0;
    }
    if (!search_parse_unsigned_decimal(arguments[2], &search_options->secret_candidate) ||
        (argument_count > 3 && !search_parse_unsigned_decimal(arguments[3], &unknown_bits)) ||
        unknown_bits < 1 || unknown_bits > FILE_SEARCH_MAX_BITS) {
        if (error_stream != NULL) {
            fprintf(error_stream, "Clave invalida o bits fuera del rango 1..%d.\n",
                    FILE_SEARCH_MAX_BITS);
        }
        return 0;
    }
    search_options->unknown_key_bits = (int)unknown_bits;
    search_options->total_candidates = UINT64_C(1) << search_options->unknown_key_bits;
    search_options->message_path = arguments[1];
    if (search_options->secret_candidate >= search_options->total_candidates) {
        if (error_stream != NULL) {
            fprintf(error_stream, "clave debe ser menor que 2^%d.\n", search_options->unknown_key_bits);
        }
        return 0;
    }
    return 1;
}

/* Revisa todo el contenido, aunque haya un byte de valor cero antes de la frase. */
static int contains_search_phrase(const unsigned char *text, size_t text_length)
{
    const size_t phrase_length = sizeof(FILE_SEARCH_PHRASE) - 1;
    for (size_t phrase_start = 0; phrase_start + phrase_length <= text_length; phrase_start++) {
        if (memcmp(text + phrase_start, FILE_SEARCH_PHRASE, phrase_length) == 0) {
            return 1;
        }
    }
    return 0;
}

void file_search_make_key(uint64_t candidate,
                          unsigned char candidate_key[AES_KEY_LENGTH])
{
    const unsigned char zero_base[AES_KEY_LENGTH] = {0};
    aes_ctr_make_candidate_key(zero_base, candidate, FILE_SEARCH_MAX_BITS, candidate_key);
}

/* Lee un byte adicional para detectar si el archivo supera el tamaño permitido. */
static int read_message(const char *message_path, unsigned char *message, size_t *message_length)
{
    FILE *message_file = fopen(message_path, "rb");
    if (message_file == NULL) {
        perror("No se pudo abrir el archivo");
        return 0;
    }
    size_t bytes_read = fread(message, 1, FILE_SEARCH_MAX_MESSAGE_LENGTH + 1, message_file);
    int read_failed = ferror(message_file);
    int close_failed = fclose(message_file);
    if (read_failed || close_failed != 0 || bytes_read > FILE_SEARCH_MAX_MESSAGE_LENGTH) {
        fprintf(stderr, "Error al leer el archivo o excede %d bytes.\n",
                FILE_SEARCH_MAX_MESSAGE_LENGTH);
        return 0;
    }
    while (bytes_read > 0 && (message[bytes_read - 1] == '\n' || message[bytes_read - 1] == '\r')) {
        bytes_read--;
    }
    if (bytes_read == 0 || !contains_search_phrase(message, bytes_read)) {
        fprintf(stderr, "El archivo esta vacio o no contiene la frase \"%s\".\n",
                FILE_SEARCH_PHRASE);
        return 0;
    }
    *message_length = bytes_read;
    return 1;
}

static void print_hexadecimal(const unsigned char *bytes, size_t byte_count)
{
    for (size_t byte_index = 0; byte_index < byte_count; byte_index++) {
        printf("%02x", (unsigned int)bytes[byte_index]);
    }
    putchar('\n');
}

int file_search_decrypt_candidate(AesCtrContext *aes_context,
                                  const FileSearchProblem *search_problem,
                                  uint64_t candidate,
                                  unsigned char decrypted_message[FILE_SEARCH_OUTPUT_CAPACITY])
{
    unsigned char candidate_key[AES_KEY_LENGTH];
    file_search_make_key(candidate, candidate_key);
    return aes_ctr_transform_buffer(aes_context, candidate_key, search_problem->initialization_vector,
                                     search_problem->ciphertext, search_problem->message_length,
                                     decrypted_message, FILE_SEARCH_OUTPUT_CAPACITY);
}

int file_search_prepare_problem(AesCtrContext *aes_context,
                                const FileSearchOptions *search_options,
                                FileSearchProblem *search_problem)
{
    unsigned char message[FILE_SEARCH_MAX_MESSAGE_LENGTH + 1];
    unsigned char secret_key[AES_KEY_LENGTH];
    unsigned char recovered_message[FILE_SEARCH_OUTPUT_CAPACITY];
    if (!read_message(search_options->message_path, message, &search_problem->message_length) ||
        RAND_bytes(search_problem->initialization_vector, AES_IV_LENGTH) != 1) {
        return 0;
    }
    file_search_make_key(search_options->secret_candidate, secret_key);
    if (!aes_ctr_transform_buffer(aes_context, secret_key, search_problem->initialization_vector,
                                   message, search_problem->message_length, search_problem->ciphertext,
                                   FILE_SEARCH_OUTPUT_CAPACITY) ||
        !file_search_decrypt_candidate(aes_context, search_problem, search_options->secret_candidate,
                                        recovered_message)) {
        return 0;
    }
    printf("Clave:            %" PRIu64 "\n", search_options->secret_candidate);
    printf("Clave AES-128:    ");
    print_hexadecimal(secret_key, AES_KEY_LENGTH);
    printf("Texto original:   ");
    fwrite(message, 1, search_problem->message_length, stdout);
    printf("\nTexto cifrado:    ");
    print_hexadecimal(search_problem->ciphertext, search_problem->message_length);
    printf("Texto recuperado: ");
    fwrite(recovered_message, 1, search_problem->message_length, stdout);
    printf("\n\n");
    return 1;
}

int file_search_test_candidate(AesCtrContext *aes_context,
                               const FileSearchProblem *search_problem,
                               uint64_t candidate, int *matches_phrase)
{
    unsigned char decrypted_message[FILE_SEARCH_OUTPUT_CAPACITY];
    if (!file_search_decrypt_candidate(aes_context, search_problem, candidate, decrypted_message)) {
        return 0;
    }
    *matches_phrase = contains_search_phrase(decrypted_message, search_problem->message_length);
    return 1;
}

void file_search_block_bounds(uint64_t total_candidates, int process_count,
                              int process_rank, uint64_t *first_candidate,
                              uint64_t *candidate_limit)
{
    uint64_t block_candidate_count = (total_candidates + (uint64_t)process_count - 1) /
                                     (uint64_t)process_count;
    uint64_t assigned_start = block_candidate_count * (uint64_t)process_rank;
    uint64_t assigned_limit = assigned_start + block_candidate_count;
    *first_candidate = assigned_start < total_candidates ? assigned_start : total_candidates;
    *candidate_limit = assigned_limit < total_candidates ? assigned_limit : total_candidates;
}

int file_search_report(AesCtrContext *aes_context, const FileSearchOptions *search_options,
                       const FileSearchProblem *search_problem,
                       const FileSearchResult *search_result, const char *approach,
                       int is_distributed_search, int process_count)
{
    unsigned char found_key[AES_KEY_LENGTH];
    unsigned char decrypted_message[FILE_SEARCH_OUTPUT_CAPACITY];
    if (search_result->found_candidate != AES_CANDIDATE_NOT_FOUND &&
        !file_search_decrypt_candidate(aes_context, search_problem,
                                       search_result->found_candidate, decrypted_message)) {
        return 0;
    }
    if (is_distributed_search) {
        printf("Ejecucion: paralela con %d procesos (%s)\n", process_count, approach);
    } else {
        printf("Ejecucion: secuencial\n");
    }
    printf("Rango explorado: 2^%d candidatas\n", search_options->unknown_key_bits);
    printf("Frase de busqueda: \"%s\"\n", FILE_SEARCH_PHRASE);
    if (search_result->found_candidate != AES_CANDIDATE_NOT_FOUND) {
        printf("Clave encontrada: candidata %" PRIu64, search_result->found_candidate);
        if (is_distributed_search) {
            printf(" (proceso %d)", search_result->winning_rank);
        }
        putchar('\n');
        file_search_make_key(search_result->found_candidate, found_key);
        printf("Clave AES-128: ");
        print_hexadecimal(found_key, AES_KEY_LENGTH);
        printf("Mensaje: ");
        fwrite(decrypted_message, 1, search_problem->message_length, stdout);
        putchar('\n');
    } else {
        printf("No se encontro la clave.\n");
    }
    printf("Candidatas probadas: %" PRIu64 "\n", search_result->candidates_tested);
    printf("Tiempo: %.9f segundos\n", search_result->elapsed_seconds);
    if (search_result->elapsed_seconds > 0.0) {
        printf("Rendimiento: %.0f candidatas/segundo\n",
               (double)search_result->candidates_tested / search_result->elapsed_seconds);
    }
    return 1;
}
