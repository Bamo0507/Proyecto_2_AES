/*
 * Cifra y descifra mensajes con ayuda de OpenSSL. También prepara el mensaje
 * del ejercicio y prueba posibles claves. Cada versión del programa decide en
 * qué orden las prueba.
 */
#ifndef AES_SEARCH_CTR_H
#define AES_SEARCH_CTR_H

#include "search_options.h"

#include <openssl/evp.h>

/* La clave y el dato de inicio del cifrado ocupan 16 bytes cada uno. */
#define AES_KEY_LENGTH 16
#define AES_IV_LENGTH 16
/* Los últimos ocho bytes permiten escribir el número de la clave que se quiere probar. */
#define AES_KEY_TAIL_BYTES 8
/* Se deja espacio adicional para que OpenSSL pueda terminar la operación. */
#define AES_OUTPUT_CAPACITY (AES_MAX_MESSAGE_LENGTH + EVP_MAX_BLOCK_LENGTH)

/*
 * Datos que OpenSSL necesita para cifrar. Se preparan una vez por trabajador y
 * se reutilizan para probar varias claves. Crear con {0}, abrir antes de
 * usarlos y cerrar al terminar. Cada trabajador debe usar sus propios datos,
 * sin copiar una instancia abierta ni compartirla entre tareas simultáneas.
 */
typedef struct {
    EVP_CIPHER *cipher;             /* Método de cifrado que se va a usar. */
    EVP_CIPHER_CTX *cipher_context; /* Datos de trabajo que cambian al cifrar o descifrar. */
} AesCtrContext;

/*
 * Datos del mensaje que van a intentar recuperar todos los trabajadores. En las
 * versiones con MPI los prepara el trabajador 0 y comparte cada arreglo por
 * separado. Solo los bytes que corresponden al tamaño del mensaje pertenecen al
 * texto cifrado; el resto es espacio adicional de trabajo.
 */
typedef struct {
    unsigned char base_key[AES_KEY_LENGTH]; /* Parte de la clave que se conserva al probar otras. */
    /* Dato de inicio que también se necesita para descifrar. */
    unsigned char initialization_vector[AES_IV_LENGTH];
    unsigned char ciphertext[AES_OUTPUT_CAPACITY]; /* Mensaje cifrado, guardado como bytes. */
} AesSearchProblem;

/*
 * Prepara OpenSSL para empezar a trabajar. Recibe aes_context creado con {0} o
 * ya cerrado. Devuelve 1 si está listo y 0 si falla. Si la preparación queda a
 * medias, libera lo que alcanzó a reservar. No abrir de nuevo una instancia que
 * todavía está en uso.
 */
int aes_ctr_context_open(AesCtrContext *aes_context);

/*
 * Libera los datos de trabajo de OpenSSL y deja sus campos en NULL. Puede
 * repetirse sobre una instancia ya cerrada; aes_context debe existir.
 */
void aes_ctr_context_close(AesCtrContext *aes_context);

/*
 * Construye una clave a partir de base_key y del número candidate. Solo cambia
 * los últimos unknown_key_bits bits, es decir, la parte final que se está
 * buscando. Conserva el resto de la clave y escribe el resultado en
 * candidate_key, un arreglo distinto de AES_KEY_LENGTH bytes.
 *
 * unknown_key_bits debe estar entre 1 y 63 y candidate debe ser menor que
 * 2^unknown_key_bits. Esas condiciones se revisan antes de llamar a esta
 * función. Se guarda primero la parte de mayor valor del número para obtener la
 * misma clave en todas las máquinas.
 */
void aes_ctr_make_candidate_key(const unsigned char base_key[AES_KEY_LENGTH],
                                uint64_t candidate, int unknown_key_bits,
                                unsigned char candidate_key[AES_KEY_LENGTH]);

/*
 * Cifra o descifra input usando key y el dato de inicio initialization_vector.
 * aes_context debe estar abierto. input_length indica cuántos bytes se leen,
 * entre 1 y AES_MAX_MESSAGE_LENGTH. output debe ser otro arreglo con
 * AES_OUTPUT_CAPACITY bytes disponibles.
 *
 * Devuelve 1 si termina bien y 0 si falla. La salida contiene bytes, sin la
 * marca final de un texto en C. Si falla, no debe utilizarse. Cada prueba
 * empieza desde el mismo dato de inicio para que una candidata no continúe
 * donde terminó la anterior.
 */
int aes_ctr_transform(AesCtrContext *aes_context,
                      const unsigned char key[AES_KEY_LENGTH],
                      const unsigned char initialization_vector[AES_IV_LENGTH],
                      const unsigned char *input, size_t input_length,
                      unsigned char output[AES_OUTPUT_CAPACITY]);

/*
 * La misma operación de cifrado, indicando también cuánto espacio hay en
 * output. La usan las versiones que leen archivos de hasta 4096 bytes.
 * aes_context debe estar abierto y key e initialization_vector tienen los
 * tamaños indicados arriba.
 *
 * input_length debe estar entre 1 e INT_MAX - EVP_MAX_BLOCK_LENGTH. output debe
 * ser distinto de input y tener al menos input_length + EVP_MAX_BLOCK_LENGTH
 * bytes. Devuelve 1 al terminar bien y 0 si falla o falta espacio; en ese caso
 * no se debe usar la salida.
 */
int aes_ctr_transform_buffer(AesCtrContext *aes_context,
                             const unsigned char key[AES_KEY_LENGTH],
                             const unsigned char initialization_vector[AES_IV_LENGTH],
                             const unsigned char *input, size_t input_length,
                             unsigned char *output, size_t output_capacity);

/*
 * Prepara el mensaje que se va a intentar recuperar. Recibe aes_context abierto
 * y search_options con los datos ya revisados. Genera una base de clave y un
 * dato de inicio nuevos, cifra el mensaje con la clave del ejercicio y guarda
 * todo en search_problem.
 *
 * En MPI solo lo hace el trabajador 0 antes de compartir los datos. Devuelve 1
 * si el problema está listo y 0 si falla; si falla, search_problem puede estar
 * incompleto.
 */
int aes_ctr_prepare_problem(AesCtrContext *aes_context, const SearchOptions *search_options,
                            AesSearchProblem *search_problem);

/*
 * Prueba si candidate recupera el comienzo conocido del mensaje. Recibe
 * aes_context abierto, las search_options revisadas y el search_problem
 * preparado con ellas. candidate debe estar entre cero y total_candidates - 1.
 *
 * Devuelve 1 si pudo hacer la prueba y 0 si falló el cifrado. La respuesta de
 * la prueba queda en matches_prefix: 1 si coincide y 0 si no. Solo se debe leer
 * esa respuesta si la función terminó bien.
 *
 * Compara como máximo ocho bytes del inicio del mensaje. Una coincidencia no
 * asegura que sea la clave original, sobre todo si el mensaje es muy corto. La
 * prueba no consulta la clave secreta usada para preparar el ejercicio.
 */
int aes_ctr_test_candidate(AesCtrContext *aes_context, const SearchOptions *search_options,
                           const AesSearchProblem *search_problem, uint64_t candidate,
                           int *matches_prefix);

#endif
