/*
 * Revisa los datos escritos al iniciar el programa. Primero comprueba el tamaño
 * de la búsqueda, después la clave del ejercicio y al final el mensaje.
 */
#include "search_options.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <string.h>

/*
 * Muestra el error en error_stream, si se indicó uno, y devuelve 0 para señalar
 * que la entrada no sirve.
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
 * Revisa que text contenga solo dígitos y que el número quepa en uint64_t.
 * Devuelve 1 y lo guarda en parsed_value si está bien; devuelve 0 sin cambiar
 * la salida si falla. La revisión inicial evita aceptar signos o espacios.
 */
int search_parse_unsigned_decimal(const char *text, uint64_t *parsed_value)
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
                         SearchOptions *search_options, FILE *error_stream)
{
    uint64_t unknown_bits_argument = AES_DEFAULT_UNKNOWN_BITS;
    if (argument_count > 4) {
        return reject_options(error_stream,
            "Uso: %s [bits] [clave_secreta] [mensaje]\n", arguments[0]);
    }
    if (argument_count > 1 &&
        !search_parse_unsigned_decimal(arguments[1], &unknown_bits_argument)) {
        return reject_options(error_stream,
            "Valor invalido para bits: %s\n", arguments[1]);
    }
    if (unknown_bits_argument < AES_MIN_UNKNOWN_BITS ||
        unknown_bits_argument > AES_MAX_UNKNOWN_BITS) {
        return reject_options(error_stream, "bits debe estar entre %d y %d.\n",
                              AES_MIN_UNKNOWN_BITS, AES_MAX_UNKNOWN_BITS);
    }

    /* Revisa el número de bits antes de usarlo para calcular cuántas claves habrá. */
    search_options->unknown_key_bits = (int)unknown_bits_argument;
    search_options->total_candidates = UINT64_C(1) << search_options->unknown_key_bits;
    /*
     * Se usa la última clave como punto de partida para un caso que recorra
     * todo el conjunto, salvo coincidencias accidentales.
     */
    search_options->secret_candidate = search_options->total_candidates - 1;
    if (argument_count > 2 &&
        !search_parse_unsigned_decimal(arguments[2], &search_options->secret_candidate)) {
        return reject_options(error_stream,
            "Valor invalido para clave_secreta: %s\n", arguments[2]);
    }
    if (search_options->secret_candidate >= search_options->total_candidates) {
        return reject_options(error_stream,
            "clave_secreta debe ser menor que 2^%d.\n", search_options->unknown_key_bits);
    }

    /* Conserva el texto de entrada sin crear una copia. */
    search_options->message = argument_count > 3 ? arguments[3] : AES_DEFAULT_MESSAGE;
    search_options->message_length = strlen(search_options->message);
    if (search_options->message_length == 0 ||
        search_options->message_length > AES_MAX_MESSAGE_LENGTH) {
        return reject_options(error_stream,
            "El mensaje debe tener entre 1 y %d bytes.\n", AES_MAX_MESSAGE_LENGTH);
    }
    /* Cuanto más corto sea el mensaje, menos información hay para reconocer la clave. */
    search_options->prefix_length = search_options->message_length < AES_KNOWN_PREFIX_LENGTH
        ? search_options->message_length : AES_KNOWN_PREFIX_LENGTH;
    return 1;
}
