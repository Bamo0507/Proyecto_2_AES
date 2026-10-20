/*
 * Lee y prepara el mensaje que comparten las búsquedas de archivos, y comprueba
 * las claves que van probando las versiones secuencial, por bloques e híbrida.
 */
#ifndef AES_FILE_SEARCH_H
#define AES_FILE_SEARCH_H

#include "aes_ctr.h"
#include "search_options.h"

#include <stdio.h>

#define FILE_SEARCH_MAX_MESSAGE_LENGTH 4096
#define FILE_SEARCH_OUTPUT_CAPACITY (FILE_SEARCH_MAX_MESSAGE_LENGTH + EVP_MAX_BLOCK_LENGTH)
#define FILE_SEARCH_MAX_BITS 56
#define FILE_SEARCH_DEFAULT_BITS 56
#define FILE_SEARCH_PHRASE "es una prueba de"
/* Valor reservado para indicar que no se encontró una coincidencia. */
#define AES_CANDIDATE_NOT_FOUND UINT64_MAX

typedef struct {
    const char *message_path;  /* Ruta del archivo tomada de la entrada; no se libera aquí. */
    uint64_t secret_candidate; /* Clave usada para preparar el mensaje del ejercicio. */
    int unknown_key_bits;      /* Tamaño de la búsqueda: entre 1 y 56 bits. */
    uint64_t total_candidates; /* Cantidad de claves posibles: 2^unknown_key_bits. */
} FileSearchOptions;

typedef struct {
    /* Dato de inicio necesario para cifrar y descifrar. */
    unsigned char initialization_vector[AES_IV_LENGTH];
    unsigned char ciphertext[FILE_SEARCH_OUTPUT_CAPACITY]; /* Mensaje cifrado, guardado como bytes. */
    size_t message_length; /* Bytes que pertenecen al mensaje, entre 1 y 4096. */
} FileSearchProblem;

typedef struct {
    uint64_t found_candidate; /* Clave elegida, o AES_CANDIDATE_NOT_FOUND si no se encontró. */
    uint64_t candidates_tested; /* Total de claves probadas entre todos los trabajadores. */
    double elapsed_seconds; /* Tiempo en segundos del trabajador que más tardó. */
    int winning_rank; /* Número del trabajador que encontró la clave; -1 si no hubo hallazgo. */
} FileSearchResult;

/*
 * Revisa <archivo.txt> <clave> [bits] al iniciar el programa. Usa 56 bits si no
 * se indican. argument_count incluye el nombre del programa; arguments contiene
 * los textos de entrada. search_options recibe los datos revisados y
 * error_stream los mensajes de error, o NULL para no mostrarlos.
 *
 * Devuelve 1 si los datos son válidos y 0 si hay un error. Solo usar
 * search_options en el primer caso. El contenido del archivo se revisa al
 * preparar el mensaje.
 */
int file_search_parse_options(int argument_count, char *arguments[],
                              FileSearchOptions *search_options, FILE *error_stream);

/*
 * Escribe el número candidate como una clave AES-128, completando el espacio
 * inicial con ceros. candidate debe ser menor que 2^56 y candidate_key debe
 * tener AES_KEY_LENGTH bytes. Se escribe la parte de mayor valor primero para
 * obtener la misma clave en distintas máquinas.
 */
void file_search_make_key(uint64_t candidate,
                          unsigned char candidate_key[AES_KEY_LENGTH]);

/*
 * Lee el archivo y revisa que contenga la frase de búsqueda. Prepara un dato de
 * inicio nuevo, cifra el mensaje y lo guarda en search_problem. aes_context
 * debe estar abierto y search_options debe haberse revisado. En MPI solo lo
 * hace el trabajador 0; la versión secuencial lo hace antes de buscar.
 *
 * Quita los saltos de línea finales y muestra el texto original, cifrado y
 * recuperado. Devuelve 1 si termina bien y 0 si falla la lectura, la revisión o
 * el cifrado. Si falla, search_problem puede quedar incompleto.
 */
int file_search_prepare_problem(AesCtrContext *aes_context,
                                const FileSearchOptions *search_options,
                                FileSearchProblem *search_problem);

/*
 * Descifra el mensaje completo con candidate y lo guarda en decrypted_message.
 * aes_context debe estar abierto y search_problem preparado; candidate debe ser
 * menor que 2^56. decrypted_message necesita FILE_SEARCH_OUTPUT_CAPACITY bytes
 * y no recibe la marca final de un texto en C. Devuelve 1 si termina bien y 0
 * si falla; solo usar la salida si terminó bien.
 */
int file_search_decrypt_candidate(AesCtrContext *aes_context,
                                  const FileSearchProblem *search_problem,
                                  uint64_t candidate,
                                  unsigned char decrypted_message[FILE_SEARCH_OUTPUT_CAPACITY]);

/*
 * Prueba una clave descifrando todo el mensaje y buscando la frase en cualquier
 * posición. Usa las mismas condiciones que file_search_decrypt_candidate().
 *
 * Devuelve 1 si pudo hacer la prueba y 0 si falló. El resultado queda en
 * matches_phrase: 1 si encontró la frase y 0 si no. Solo se debe leer ese
 * resultado cuando la función termine bien.
 */
int file_search_test_candidate(AesCtrContext *aes_context,
                               const FileSearchProblem *search_problem,
                               uint64_t candidate, int *matches_phrase);

/*
 * Calcula qué grupo de claves le toca a process_rank entre process_count
 * trabajadores. first_candidate recibe la primera clave y candidate_limit la
 * primera que queda fuera del grupo. total_candidates debe estar entre 1 y
 * 2^56. process_count debe ser al menos 1 y el número del trabajador debe estar
 * entre 0 y process_count - 1.
 *
 * Los grupos con trabajo tienen el mismo tamaño previsto, salvo el último de
 * ellos. Si hay más trabajadores que claves, algunos reciben un grupo vacío.
 */
void file_search_block_bounds(uint64_t total_candidates, int process_count,
                              int process_rank, uint64_t *first_candidate,
                              uint64_t *candidate_limit);

/*
 * Muestra las claves probadas, el tiempo y el mensaje recuperado de las
 * versiones que leen un archivo. Recibe aes_context abierto, search_options y
 * search_problem del mismo ejercicio, y search_result después de reunir las
 * mediciones. approach es el nombre de la versión, is_distributed_search indica
 * si se usa MPI y process_count la cantidad de trabajadores.
 *
 * En MPI solo se llama desde el trabajador 0. Se usa fuera del tiempo medido.
 * Devuelve 0 si falla el descifrado final y 1 en los demás casos. No revisa
 * errores de impresión.
 */
int file_search_report(AesCtrContext *aes_context, const FileSearchOptions *search_options,
                       const FileSearchProblem *search_problem,
                       const FileSearchResult *search_result, const char *approach,
                       int is_distributed_search, int process_count);

#endif
