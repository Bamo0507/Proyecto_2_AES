/*
 * Reparte la búsqueda híbrida y avisa cuándo deben detenerse los trabajadores.
 */
#ifndef AES_HYBRID_SEARCH_MPI_H
#define AES_HYBRID_SEARCH_MPI_H

#include "file_search_mpi.h"
#include "hybrid_order.h"

typedef struct {
    FileSearchResult summary; /* Resultado general reunido por el trabajador 0. */
    uint64_t sequential_candidates; /* Claves probadas en orden consecutivo, sin repeticiones. */
    uint64_t permuted_candidates; /* Claves probadas en el otro orden, sin repeticiones. */
} HybridSearchResult;

/*
 * Busca dentro de los grupos de claves, alternando k pruebas consecutivas con
 * una en otro orden. k se recibe en sequential_trials_per_turn. Cada trabajador
 * usa aes_context abierto y propio, las mismas opciones de búsqueda y el mismo
 * mensaje preparado. process_rank es su número entre los process_count
 * participantes. Todos deben entrar a esta función; k y la cantidad total de
 * claves deben ser mayores que cero.
 *
 * seed y el número del trabajador eligen su orden de búsqueda. Cuando alguien
 * encuentra una coincidencia o termina su grupo, avisa al trabajador 0. Los que
 * ya terminaron esperan la decisión final; todos completan sus avisos antes de
 * reunir los resultados.
 * El trabajo se divide en tandas que incluyen los cálculos para saltar claves
 * repetidas. Entre tandas se revisan los avisos y se retoma el avance guardado.
 *
 * Devuelve la misma clave elegida a todos. Las cantidades sumadas y el tiempo
 * final solo se leen en el trabajador 0. Un fallo que impida continuar detiene
 * a todos. Cada grupo recuerda su avance con una cantidad fija de datos; el
 * trabajador 0 guarda además un aviso de parada por cada compañero.
 */
HybridSearchResult mpi_hybrid_search(AesCtrContext *aes_context,
                                     const FileSearchOptions *search_options,
                                     const FileSearchProblem *search_problem,
                                     uint64_t sequential_trials_per_turn, uint64_t seed,
                                     int process_rank, int process_count);

#endif
