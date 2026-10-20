/*
 * Cifra y descifra mensajes con ayuda de OpenSSL y construye las claves que
 * comparten las búsquedas.
 */
#ifndef AES_SEARCH_CTR_H
#define AES_SEARCH_CTR_H

#include <stddef.h>
#include <stdint.h>

#include <openssl/evp.h>

/* La clave y el dato de inicio del cifrado ocupan 16 bytes cada uno. */
#define AES_KEY_LENGTH 16
#define AES_IV_LENGTH 16
/* Los últimos ocho bytes permiten escribir el número de la clave que se quiere probar. */
#define AES_KEY_TAIL_BYTES 8

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
 * output_capacity indica cuánto espacio hay en output. Las búsquedas de
 * archivos admiten mensajes de hasta 4096 bytes.
 * aes_context debe estar abierto y key e initialization_vector tienen los
 * tamaños indicados arriba.
 *
 * input_length debe estar entre 1 e INT_MAX - EVP_MAX_BLOCK_LENGTH. output debe
 * ser distinto de input y tener al menos input_length + EVP_MAX_BLOCK_LENGTH
 * bytes. Devuelve 1 al terminar bien y 0 si falla o falta espacio; en ese caso
 * no se debe usar la salida. El resultado contiene bytes sin una marca final
 * de texto. Cada llamada comienza desde initialization_vector para no continuar
 * donde terminó la prueba anterior.
 */
int aes_ctr_transform_buffer(AesCtrContext *aes_context,
                             const unsigned char key[AES_KEY_LENGTH],
                             const unsigned char initialization_vector[AES_IV_LENGTH],
                             const unsigned char *input, size_t input_length,
                             unsigned char *output, size_t output_capacity);

#endif
