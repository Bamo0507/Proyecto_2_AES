/*
 * Reúne los datos de entrada de las versiones originales: cuántas claves
 * probar, qué mensaje usar y con qué clave preparar el ejercicio.
 */
#ifndef AES_SEARCH_OPTIONS_H
#define AES_SEARCH_OPTIONS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Cantidad de bits que se van a buscar; el resto de la clave se mantiene igual. */
#define AES_MIN_UNKNOWN_BITS 1
#define AES_MAX_UNKNOWN_BITS 40
#define AES_DEFAULT_UNKNOWN_BITS 24
/* Tamaños en bytes, sin contar la marca final del texto en C. */
#define AES_MAX_MESSAGE_LENGTH 256
#define AES_KNOWN_PREFIX_LENGTH 8
#define AES_DEFAULT_MESSAGE "Puedes lograrlo!"
/* Valor reservado para indicar que no se encontró una coincidencia. */
#define AES_CANDIDATE_NOT_FOUND UINT64_MAX

/*
 * Datos que necesita la búsqueda después de revisar la entrada.
 * search_options_parse() calcula los tamaños y comprueba los límites. message
 * sigue usando el texto original; esta estructura no crea una copia ni debe
 * liberarlo.
 */
typedef struct {
    int unknown_key_bits;       /* Parte de la clave que falta encontrar: entre 1 y 40 bits. */
    uint64_t total_candidates; /* Cantidad de claves posibles: 2^unknown_key_bits. */
    uint64_t secret_candidate; /* Clave que se usa para preparar el mensaje cifrado. */
    const char *message;       /* Mensaje original que se quiere recuperar. */
    size_t message_length;     /* Tamaño del mensaje, entre 1 y 256 bytes. */
    size_t prefix_length;      /* Cantidad de bytes del comienzo que se van a comparar. */
} SearchOptions;

/*
 * Lee [bits] [clave_secreta] [mensaje] escritos al iniciar el programa. Si
 * faltan, usa 24 bits, la última clave posible y el mensaje predeterminado. Los
 * números deben contener solo dígitos y el mensaje debe ocupar entre 1 y 256
 * bytes.
 *
 * argument_count cuenta también el nombre del programa y arguments contiene
 * esos textos. search_options recibe los datos revisados. error_stream indica
 * dónde mostrar errores; usar NULL para no imprimirlos en los demás
 * trabajadores.
 *
 * Devuelve 1 si la entrada está bien y 0 si hay un error. Solo utilizar
 * search_options cuando devuelva 1. El mensaje sigue apuntando al texto de
 * entrada, que debe conservarse mientras dure la búsqueda.
 */
int search_options_parse(int argument_count, char *arguments[],
                         SearchOptions *search_options, FILE *error_stream);

/*
 * Convierte text, escrito solo con dígitos decimales, en un número sin signo de
 * hasta 64 bits. Guarda el número en parsed_value y devuelve 1 si pudo
 * convertirlo. Devuelve 0 si hay signos, espacios, otros caracteres o un número
 * demasiado grande; en ese caso no cambia parsed_value.
 */
int search_parse_unsigned_decimal(const char *text, uint64_t *parsed_value);

#endif
