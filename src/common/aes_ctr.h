/**
 * @file aes_ctr.h
 * @brief Operaciones criptograficas del ejercicio de busqueda AES-128-CTR.
 *
 * El modulo prepara el problema y verifica una candidata por llamada. No decide
 * el orden de busqueda ni comunica resultados entre procesos. Todas las funciones
 * requieren punteros validos; los errores se devuelven al ejecutable llamador.
 */
#ifndef AES_SEARCH_CTR_H
#define AES_SEARCH_CTR_H

#include "search_options.h"

#include <openssl/evp.h>

/* AES-128 utiliza una clave de 16 bytes; el contador inicial CTR ocupa 16 bytes. */
#define AES_KEY_LENGTH 16
#define AES_IV_LENGTH 16
/* Cola de clave interpretada como entero big-endian para insertar la candidata. */
#define AES_KEY_TAIL_BYTES 8
/* Reserva adicional para las operaciones Update/Final de la interfaz EVP. */
#define AES_OUTPUT_CAPACITY (AES_MAX_MESSAGE_LENGTH + EVP_MAX_BLOCK_LENGTH)

/**
 * @brief Recursos OpenSSL propiedad de un trabajador.
 *
 * Inicializar con {0}, abrir una vez por proceso y cerrar al terminar. El contexto
 * se reutiliza entre candidatas para evitar reservarlo en cada iteracion.
 * No copiar una instancia abierta ni compartirla entre hilos: cada trabajador
 * debe ser propietario de sus recursos y liberarlos una sola vez.
 */
typedef struct {
    EVP_CIPHER *cipher;             /**< Descriptor AES-128-CTR obtenido con fetch. */
    EVP_CIPHER_CTX *cipher_context; /**< Estado mutable de la operacion EVP actual. */
} AesCtrContext;

/**
 * @brief Datos del mismo problema que deben conocer todos los trabajadores.
 *
 * En MPI los genera rank 0 y se transmiten los arreglos por separado, evitando
 * depender del padding o layout de la estructura. No contiene punteros propios.
 * La longitud util de ciphertext esta en SearchOptions.message_length; la
 * capacidad restante del arreglo no forma parte del mensaje cifrado.
 */
typedef struct {
    unsigned char base_key[AES_KEY_LENGTH]; /**< Base para reconstruir cada clave. */
    unsigned char initialization_vector[AES_IV_LENGTH]; /**< Contador inicial publico. */
    unsigned char ciphertext[AES_OUTPUT_CAPACITY]; /**< Mensaje cifrado, sin '\0'. */
} AesSearchProblem;

/**
 * @brief Reserva el descriptor AES-128-CTR y su contexto de trabajo.
 * @param[in,out] context Instancia inicializada con {0} o previamente cerrada.
 * @return 1 si ambos recursos estan disponibles; 0 si falla alguna reserva.
 *
 * En error libera cualquier reserva parcial y deja ambos punteros en NULL.
 * No llamar sobre una instancia abierta: se perderian sus recursos anteriores.
 */
int aes_ctr_context_open(AesCtrContext *context);

/**
 * @brief Libera los recursos del contexto y deja sus punteros en NULL.
 * @param[in,out] context Instancia abierta, cerrada o inicializada con {0}.
 *
 * Puede llamarse varias veces sobre la misma instancia; context no puede ser NULL.
 */
void aes_ctr_context_close(AesCtrContext *context);

/**
 * @brief Reconstruye una clave reemplazando los bits inferiores de una base.
 * @param[in] base_key Base de AES_KEY_LENGTH bytes, conservada por la funcion.
 * @param[in] candidate Entero que representa los bits desconocidos de la clave.
 * @param[in] unknown_key_bits Cantidad de bits inferiores que se reemplazan.
 * @param[out] candidate_key Destino de AES_KEY_LENGTH bytes, distinto de base_key.
 * @pre unknown_key_bits esta entre 1 y 40. Para la busqueda, candidate debe ser
 *      menor que 2^unknown_key_bits; esta funcion solo aplica la mascara.
 *
 * Los demas bits se conservan. La cola se lee y escribe en orden big-endian,
 * independientemente de la arquitectura de la maquina. No genera aleatoriedad
 * ni valida rangos: esos controles corresponden a SearchOptions.
 */
void aes_ctr_make_candidate_key(const unsigned char base_key[AES_KEY_LENGTH],
                                uint64_t candidate, int unknown_key_bits,
                                unsigned char candidate_key[AES_KEY_LENGTH]);

/**
 * @brief Cifra o descifra bytes mediante AES-128-CTR, sin relleno.
 * @param[in,out] context Contexto abierto; su estado EVP cambia en cada llamada.
 * @param[in] key Clave completa de AES_KEY_LENGTH bytes.
 * @param[in] initialization_vector Contador inicial de AES_IV_LENGTH bytes.
 * @param[in] input Entrada con al menos input_length bytes legibles.
 * @param[in] input_length Cantidad de bytes, entre 1 y AES_MAX_MESSAGE_LENGTH.
 * @param[out] output Buffer separado de input, con AES_OUTPUT_CAPACITY bytes.
 * @return 1 si se produjeron exactamente input_length bytes; 0 si la longitud
 *         es invalida, falla EVP o la longitud de salida es inesperada.
 *
 * CTR usa la misma operacion para cifrar y descifrar. Reinicializar clave e IV
 * impide arrastrar el contador de una candidata a la siguiente. El resultado es
 * binario y no recibe terminador nulo; si falla la operacion no debe utilizarse.
 * La funcion no imprime errores ni termina el proceso. Los errores de OpenSSL
 * pueden consultarse desde el ejecutable con ERR_print_errors_fp().
 */
int aes_ctr_transform(AesCtrContext *context,
                      const unsigned char key[AES_KEY_LENGTH],
                      const unsigned char initialization_vector[AES_IV_LENGTH],
                      const unsigned char *input, size_t input_length,
                      unsigned char output[AES_OUTPUT_CAPACITY]);

/**
 * @brief Genera la base y el IV aleatorios y cifra el mensaje del ejercicio.
 * @param[in,out] context Contexto abierto para cifrar la entrada conocida.
 * @param[in] options Opciones validadas; secret_candidate solo prepara el cifrado.
 * @param[out] problem Destino de la base, el IV y el texto cifrado.
 * @return 1 si el problema esta listo; 0 si falla RAND_bytes() o el cifrado.
 *
 * En error problem puede quedar parcialmente escrito y debe descartarse.
 * En secuencial se ejecuta una vez; en MPI solo rank 0 debe invocarla y luego
 * compartir los datos. Cada llamada genera un problema aleatorio nuevo.
 */
int aes_ctr_prepare_problem(AesCtrContext *context, const SearchOptions *options,
                            AesSearchProblem *problem);

/**
 * @brief Comprueba si una candidata descifra el prefijo conocido del mensaje.
 * @param[in,out] context Contexto abierto, exclusivo del trabajador actual.
 * @param[in] options Opciones validadas, con mensaje y longitud del prefijo.
 * @param[in] problem Problema preparado con las mismas opciones.
 * @param[in] candidate Valor entre 0 y options->total_candidates - 1.
 * @param[out] matches_prefix Recibe 1 si coincide el prefijo, o 0 si no coincide.
 * @return 1 si se pudo evaluar la candidata; 0 si fallo la operacion EVP.
 *
 * Un retorno 1 no significa que se encontro una coincidencia: hay que consultar
 * matches_prefix, cuyo contenido solo es valido cuando la funcion tiene exito.
 * No se consulta options->secret_candidate para decidir la coincidencia.
 *
 * Se descifran hasta 8 bytes, sin procesar el resto. Una coincidencia identifica
 * una candidata compatible y no demuestra unicidad. Con mensajes mas cortos se
 * compara un prefijo menor y aumenta la posibilidad de coincidencias accidentales.
 */
int aes_ctr_test_candidate(AesCtrContext *context, const SearchOptions *options,
                           const AesSearchProblem *problem, uint64_t candidate,
                           int *matches_prefix);

#endif
