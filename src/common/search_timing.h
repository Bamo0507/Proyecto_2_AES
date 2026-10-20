/* Consulta el mismo reloj para medir las búsquedas secuenciales. */
#ifndef AES_SEARCH_TIMING_H
#define AES_SEARCH_TIMING_H

/*
 * Guarda la lectura del reloj en clock_seconds. La diferencia entre dos
 * lecturas indica cuánto tiempo pasó, sin depender de cambios de la hora.
 * Devuelve 1 si pudo consultar el reloj y 0 si falla; solo usar la lectura
 * cuando devuelve 1.
 */
int search_monotonic_seconds(double *clock_seconds);

#endif
