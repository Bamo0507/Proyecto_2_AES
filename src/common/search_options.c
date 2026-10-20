/* Revisa los números escritos al iniciar las búsquedas. */
#include "search_options.h"

#include <errno.h>
#include <inttypes.h>

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
