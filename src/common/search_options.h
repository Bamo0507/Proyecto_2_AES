/* Conversión de los argumentos numéricos que comparten las búsquedas. */
#ifndef AES_SEARCH_OPTIONS_H
#define AES_SEARCH_OPTIONS_H

#include <stdint.h>

/*
 * Convierte text, escrito solo con dígitos decimales, en un número sin signo de
 * hasta 64 bits. Guarda el número en parsed_value y devuelve 1 si pudo
 * convertirlo. Devuelve 0 si hay signos, espacios, otros caracteres o un número
 * demasiado grande; en ese caso no cambia parsed_value.
 */
int search_parse_unsigned_decimal(const char *text, uint64_t *parsed_value);

#endif
