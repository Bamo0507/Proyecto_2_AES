/*
 * Decide el orden de las claves dentro de un grupo. Alterna k pruebas
 * consecutivas con una en otro orden y evita repetir claves. Recuerda el avance
 * con unos pocos números, sin guardar una lista de todas las claves visitadas.
 */
#ifndef AES_HYBRID_ORDER_H
#define AES_HYBRID_ORDER_H

#include <stdint.h>

#define HYBRID_DEFAULT_SEQUENTIAL_TRIALS UINT64_C(16)
#define HYBRID_DEFAULT_SEED UINT64_C(1)
#define HYBRID_MAX_BLOCK_SIZE (UINT64_C(1) << 56)

typedef enum {
    HYBRID_SEQUENTIAL,
    HYBRID_PERMUTED
} HybridCandidateSource;

typedef enum {
    HYBRID_ORDER_FINISHED,
    HYBRID_CANDIDATE_READY,
    HYBRID_ORDER_PAUSED
} HybridOrderStatus;

typedef struct {
    uint64_t block_candidate_count; /* Cantidad de claves del grupo; puede ser cero. */
    uint64_t permutation_mask; /* Conserva solo los bits que necesita el cálculo del orden. */
    uint64_t permutation_multiplier; /* Número impar usado para reordenar las claves. */
    uint64_t inverse_permutation_multiplier; /* Número que permite deshacer ese reordenamiento. */
    uint64_t permutation_shift; /* Cambio de posición elegido a partir de la semilla. */
    uint64_t next_sequential_position; /* Próxima posición que revisará el recorrido consecutivo. */
    uint64_t next_permutation_index; /* Próximo lugar que revisará el recorrido en otro orden. */
    uint64_t sequential_trials_per_turn; /* Cantidad de pruebas consecutivas por turno: k. */
    uint64_t sequential_trials_remaining; /* Pruebas consecutivas que faltan en el turno actual. */
    uint64_t candidates_emitted; /* Claves distintas entregadas para probar hasta ahora. */
    uint64_t pending_position; /* Posición desde la que empezó el cálculo actual. */
    uint64_t mapped_position; /* Resultado parcial que se conserva al pausar. */
    HybridCandidateSource pending_source; /* Recorrido que inició ese cálculo. */
    int mapping_in_progress; /* Indica si hay un cálculo que se debe retomar. */
} HybridOrder;

/*
 * Prepara el recorrido de un grupo de block_candidate_count claves.
 * sequential_trials_per_turn es k: cuántas pruebas consecutivas se hacen antes
 * de una en otro orden. seed elige ese otro orden; repetirla permite obtener el
 * mismo recorrido.
 *
 * El grupo puede tener de cero a HYBRID_MAX_BLOCK_SIZE claves; k debe ser mayor
 * que cero y seed puede ser cualquier uint64_t. Devuelve 1 si preparó el
 * recorrido y 0 si los valores son inválidos. No usar search_order si devuelve
 * 0. No hay memoria que liberar al terminar.
 */
int hybrid_order_initialize(HybridOrder *search_order, uint64_t block_candidate_count,
                            uint64_t sequential_trials_per_turn, uint64_t seed);

/*
 * La primera función convierte un lugar del recorrido en la posición de la
 * clave que aparece allí. La segunda permite hacer el camino de vuelta y saber
 * en qué lugar del recorrido aparece esa posición.
 *
 * search_order debe estar preparado y su grupo no debe estar vacío. Ambas
 * funciones reciben y devuelven números entre cero y block_candidate_count
 * menos uno. Son posiciones dentro del grupo, no números de clave completos.
 * Estas funciones no cambian el avance de la búsqueda. Si un cálculo sale del
 * grupo, se repite hasta volver a una posición válida.
 */
uint64_t hybrid_order_permute(const HybridOrder *search_order, uint64_t permutation_index);
uint64_t hybrid_order_inverse(const HybridOrder *search_order, uint64_t candidate_position);

/*
 * Guarda en selected_position la siguiente posición que todavía no se ha
 * probado y señala en candidate_source cuál de los dos recorridos la eligió.
 * Para obtener el número de clave hay que sumar la primera clave del grupo. Por
 * ejemplo, la posición 3 del grupo que empieza en 100 corresponde a 103.
 * search_order debe estar preparado y se conserva entre llamadas.
 * calculations_remaining indica cuántos cálculos más permite hacer quien
 * llama. Cada cálculo de una posición consume uno, aunque corresponda a una
 * clave repetida o haya que calcular de nuevo para volver al grupo.
 *
 * Devuelve HYBRID_CANDIDATE_READY si entregó una posición,
 * HYBRID_ORDER_FINISHED al terminar el grupo y HYBRID_ORDER_PAUSED si se agotó
 * el saldo. En una pausa conserva el avance, incluido cualquier cálculo a
 * medias. Se puede revisar si hay avisos y llamar de nuevo con otro saldo.
 * Solo se escriben selected_position y candidate_source al entregar una clave.
 * Con saldo cero, si queda trabajo, devuelve una pausa sin avanzar.
 *
 * Cada recorrido salta las claves que el otro ya había probado. k cuenta
 * pruebas nuevas; las posiciones que se saltan no consumen una prueba del
 * turno.
 */
HybridOrderStatus hybrid_order_next(HybridOrder *search_order,
                                    uint64_t *calculations_remaining,
                                    uint64_t *selected_position,
                                    HybridCandidateSource *candidate_source);

#endif
