/**
 * @file search_reporting.h
 * @brief Resultado y presentacion compartidos por las implementaciones CTR.
 *
 * El ejecutable recoge las estadisticas; este modulo reconstruye la clave
 * compatible, descifra el mensaje completo e imprime el resultado fuera del reloj.
 */
#ifndef AES_SEARCH_REPORTING_H
#define AES_SEARCH_REPORTING_H

#include "aes_ctr.h"

/**
 * @brief Estadisticas de una busqueda terminada.
 *
 * Inicializar como {AES_CANDIDATE_NOT_FOUND, 0, 0.0}. En MPI, los conteos y el
 * tiempo agregado solo son validos en rank 0 despues de las reducciones.
 */
typedef struct {
    uint64_t found_candidate; /**< Candidata compatible o AES_CANDIDATE_NOT_FOUND. */
    uint64_t candidates_tested; /**< Pruebas realizadas; suma de todos los ranks en MPI. */
    double elapsed_seconds; /**< Duracion en segundos; maximo de tiempos locales en MPI. */
} SearchResult;

/**
 * @brief Descifra el resultado, si existe, e imprime el reporte por stdout.
 * @param[in,out] context Contexto abierto; se reutiliza para el descifrado final.
 * @param[in] options Opciones validadas con el mensaje y su longitud.
 * @param[in] problem Problema utilizado durante la busqueda.
 * @param[in] result Resultado terminado; si hay candidata, debe estar en rango.
 * @param[in] distributed 0 para el formato secuencial; distinto de 0 para MPI.
 * @param[in] process_count Cantidad de procesos; usar 1 en secuencial.
 * @pre process_count >= 1; elapsed_seconds >= 0. Llamar fuera del intervalo
 *      medido y, en MPI, solo desde rank 0 con estadisticas ya agregadas.
 * @return 0 si falla el descifrado final; 1 si no se requiere descifrar o si
 *         se completo esa operacion. No detecta errores de escritura en stdout.
 *
 * El rank mostrado se calcula como found_candidate % process_count: supone
 * el reparto ciclico actual. Una estrategia por bloques requeriria adaptar ese
 * dato. El rendimiento usa candidatas/segundo y se omite si el tiempo es cero.
 *
 * El descifrado completo solo presenta la candidata elegida: no comprueba otra
 * vez el mensaje ni garantiza recuperar la candidata secreta original.
 */
int search_report(AesCtrContext *context, const SearchOptions *options,
                   const AesSearchProblem *problem, const SearchResult *result,
                   int distributed, int process_count);

#endif
