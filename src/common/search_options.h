/**
 * @file search_options.h
 * @brief Configuracion y validacion compartidas por las busquedas CTR.
 *
 * Define el espacio de candidatas y el mensaje conocido. No realiza cifrado,
 * mediciones ni llamadas MPI. Cada ejecutable decide como recorrer ese espacio.
 */
#ifndef AES_SEARCH_OPTIONS_H
#define AES_SEARCH_OPTIONS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Limites del ejercicio; el resto de los 128 bits de la clave permanece fijo. */
#define AES_MIN_UNKNOWN_BITS 1
#define AES_MAX_UNKNOWN_BITS 40
#define AES_DEFAULT_UNKNOWN_BITS 24
/* Longitudes en bytes, sin incluir el terminador nulo del mensaje. */
#define AES_MAX_MESSAGE_LENGTH 256
#define AES_KNOWN_PREFIX_LENGTH 8
#define AES_DEFAULT_MESSAGE "Puedes lograrlo!"
/* Fuera de todo rango permitido: ninguna candidata valida alcanza este valor. */
#define AES_CANDIDATE_NOT_FOUND UINT64_MAX

/**
 * @brief Opciones listas para preparar el problema y ejecutar una busqueda.
 *
 * search_options_parse() establece las relaciones entre estos campos. Si se
 * construye una instancia manualmente, se deben conservar las mismas relaciones.
 * La estructura no reserva memoria ni es propietaria del texto del mensaje.
 */
typedef struct {
    int unknown_key_bits;       /**< Bits inferiores por explorar: entre 1 y 40. */
    uint64_t total_candidates; /**< 2^unknown_key_bits; limite superior exclusivo. */
    uint64_t secret_candidate; /**< Candidata usada para cifrar, menor al limite. */
    const char *message;       /**< Texto prestado de argv o de un literal. */
    size_t message_length;     /**< strlen(message), entre 1 y 256 bytes. */
    size_t prefix_length;      /**< min(message_length, AES_KNOWN_PREFIX_LENGTH). */
} SearchOptions;

/**
 * @brief Interpreta los argumentos posicionales [bits] [clave_secreta] [mensaje].
 *
 * Si faltan argumentos, usa 24 bits, la ultima candidata y AES_DEFAULT_MESSAGE.
 * Los numeros admiten solo digitos decimales; se rechazan signos, espacios,
 * desbordamientos, caracteres sobrantes y valores fuera de rango. El mensaje
 * se mide en bytes, no en caracteres, y debe contener entre 1 y 256 bytes.
 *
 * @param[in] argument_count Cantidad de argumentos, incluido el ejecutable.
 * @param[in] arguments Vector argv con strings terminados en nulo.
 * @param[out] options Destino de la configuracion validada.
 * @param[in] error_stream Flujo para diagnosticos, o NULL para no imprimir.
 * @pre argument_count >= 1; arguments, arguments[0] y options son validos.
 * @return 1 si todos los campos de options son validos; 0 en caso contrario.
 *
 * En error, options puede quedar parcialmente escrita y no debe utilizarse.
 * El llamador debe mantener vigente arguments mientras se use options->message.
 * La funcion no libera recursos ni termina el proceso. MPI puede pasar stderr
 * en rank 0 y NULL en los demas para evitar mensajes repetidos.
 */
int search_options_parse(int argument_count, char *arguments[],
                         SearchOptions *options, FILE *error_stream);

#endif
