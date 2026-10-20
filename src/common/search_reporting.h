/*
 * Muestra la clave elegida, el mensaje recuperado y las mediciones de las
 * versiones originales. Recibe los resultados después de terminar la búsqueda.
 */
#ifndef AES_SEARCH_REPORTING_H
#define AES_SEARCH_REPORTING_H

#include "aes_ctr.h"

/*
 * Resultado de una búsqueda. Al comenzar se usa {AES_CANDIDATE_NOT_FOUND, 0,
 * 0.0}. Cuando trabajan varios procesos, el trabajador 0 reúne las cantidades y
 * los tiempos antes de mostrar estos datos.
 */
typedef struct {
    uint64_t found_candidate; /* Clave elegida, o AES_CANDIDATE_NOT_FOUND si no hay coincidencia. */
    uint64_t candidates_tested; /* Cantidad de claves probadas entre todos los trabajadores. */
    double elapsed_seconds; /* Tiempo en segundos; se usa el del trabajador que más tardó. */
} SearchResult;

/*
 * Muestra el resultado y descifra el mensaje completo con la clave elegida.
 * Recibe aes_context abierto, search_options y search_problem del mismo
 * ejercicio, y search_result con la búsqueda terminada. is_distributed_search
 * indica si se usa la versión distribuida y process_count cuántos trabajadores
 * participan; debe ser al menos 1.
 *
 * Se llama después de medir el tiempo y, en MPI, solo desde el trabajador 0.
 * Devuelve 0 si falla el descifrado y 1 en los demás casos. No revisa si hubo
 * errores al imprimir.
 *
 * Para identificar al trabajador que encontró la clave supone que se
 * repartieron por turnos; usa found_candidate % process_count. El mensaje
 * completo se muestra sin volver a comprobar si la clave coincide con la
 * original.
 */
int search_report(AesCtrContext *aes_context, const SearchOptions *search_options,
                   const AesSearchProblem *search_problem, const SearchResult *search_result,
                   int is_distributed_search, int process_count);

#endif
