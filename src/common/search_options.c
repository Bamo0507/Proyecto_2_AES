/**
 * @file search_options.c
 * @brief Convierte la CLI en una configuracion consistente para ambas busquedas.
 *
 * Primero valida el numero de bits, despues el rango de la candidata secreta y
 * finalmente el mensaje. Los valores derivados solo se calculan cuando sus
 * entradas son validas. Los contratos publicos estan en search_options.h.
 */
#include "search_options.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>

/*
 * Emite un diagnostico con formato printf si hay flujo de salida. Devuelve
 * siempre 0 para propagar el error directamente desde search_options_parse().
 * format y sus argumentos deben corresponderse; error_stream puede ser NULL.
 */
static int reject_options(FILE *error_stream, const char *format, ...)
{
    if (error_stream != NULL) {
        va_list arguments;
        va_start(arguments, format);
        vfprintf(error_stream, format, arguments);
        va_end(arguments);
    }
    return 0;
}

/*
 * Valida y convierte un string decimal a uint64_t. text y parsed_value deben
 * ser validos; el destino solo se escribe en exito. Devuelve 1 en exito, 0 si
 * no hay digitos, hay otro caracter o el valor no cabe en uint64_t.
 *
 * El recorrido previo rechaza signos y espacios que strtoumax podria aceptar;
 * errno y el limite de uint64_t cubren los desbordamientos de la conversion.
 */
static int parse_unsigned_decimal(const char *text, uint64_t *parsed_value)
{
    if (text[0] == '\0') {
        return 0;
    }
    for (const char *character = text; *character != '\0'; character++) {
        if (*character < '0' || *character > '9') {
            return 0;
        }
    }

    char *parse_end;
    errno = 0;
    uintmax_t converted_value = strtoumax(text, &parse_end, 10);
    if (errno != 0 || *parse_end != '\0' || converted_value > UINT64_MAX) {
        return 0;
    }
    *parsed_value = (uint64_t)converted_value;
    return 1;
}

int search_options_parse(int argument_count, char *arguments[],
                         SearchOptions *options, FILE *error_stream)
{
    uint64_t unknown_bits_argument = AES_DEFAULT_UNKNOWN_BITS;
    if (argument_count > 4) {
        return reject_options(error_stream,
            "Uso: %s [bits] [clave_secreta] [mensaje]\n", arguments[0]);
    }
    if (argument_count > 1 &&
        !parse_unsigned_decimal(arguments[1], &unknown_bits_argument)) {
        return reject_options(error_stream,
            "Valor invalido para bits: %s\n", arguments[1]);
    }
    if (unknown_bits_argument < AES_MIN_UNKNOWN_BITS ||
        unknown_bits_argument > AES_MAX_UNKNOWN_BITS) {
        return reject_options(error_stream, "bits debe estar entre %d y %d.\n",
                              AES_MIN_UNKNOWN_BITS, AES_MAX_UNKNOWN_BITS);
    }

    /* Validar antes del cast y del desplazamiento evita cantidades fuera de rango. */
    options->unknown_key_bits = (int)unknown_bits_argument;
    options->total_candidates = UINT64_C(1) << options->unknown_key_bits;
    /* La ultima candidata fuerza a recorrer todo el espacio en el caso normal. */
    options->secret_candidate = options->total_candidates - 1;
    if (argument_count > 2 &&
        !parse_unsigned_decimal(arguments[2], &options->secret_candidate)) {
        return reject_options(error_stream,
            "Valor invalido para clave_secreta: %s\n", arguments[2]);
    }
    if (options->secret_candidate >= options->total_candidates) {
        return reject_options(error_stream,
            "clave_secreta debe ser menor que 2^%d.\n", options->unknown_key_bits);
    }

    /* Se presta el string original: no se crea una copia que haya que liberar. */
    options->message = argument_count > 3 ? arguments[3] : AES_DEFAULT_MESSAGE;
    options->message_length = strlen(options->message);
    if (options->message_length == 0 ||
        options->message_length > AES_MAX_MESSAGE_LENGTH) {
        return reject_options(error_stream,
            "El mensaje debe tener entre 1 y %d bytes.\n", AES_MAX_MESSAGE_LENGTH);
    }
    /* Un mensaje corto reduce la evidencia disponible para verificar una clave. */
    options->prefix_length = options->message_length < AES_KNOWN_PREFIX_LENGTH
        ? options->message_length : AES_KNOWN_PREFIX_LENGTH;
    return 1;
}
