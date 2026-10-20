/*
 * Reordena las posiciones de las claves y permite volver al lugar que tenía
 * cada una. Con esos dos cálculos se reconoce si una clave ya se probó por el
 * otro recorrido. Si el resultado cae fuera del grupo, se vuelve a calcular
 * hasta regresar a él.
 */
#include "hybrid_order.h"

/* Mezcla la semilla para variar el orden de forma repetible; no se usa para cifrar. */
static uint64_t mix_seed(uint64_t seed_value)
{
    seed_value = (seed_value ^ (seed_value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    seed_value = (seed_value ^ (seed_value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return seed_value ^ (seed_value >> 31);
}

int hybrid_order_initialize(HybridOrder *search_order, uint64_t block_candidate_count,
                            uint64_t sequential_trials_per_turn, uint64_t seed)
{
    if (block_candidate_count > HYBRID_MAX_BLOCK_SIZE || sequential_trials_per_turn == 0) {
        return 0;
    }
    uint64_t permutation_domain_size = 1;
    while (permutation_domain_size < block_candidate_count) {
        permutation_domain_size <<= 1;
    }
    *search_order = (HybridOrder){0};
    search_order->block_candidate_count = block_candidate_count;
    search_order->permutation_mask = permutation_domain_size - 1;
    /* El multiplicador debe ser impar para que el cálculo se pueda deshacer. */
    search_order->permutation_multiplier = mix_seed(seed) | UINT64_C(1);
    search_order->permutation_shift =
        mix_seed(seed + UINT64_C(0x9e3779b97f4a7c15)) & search_order->permutation_mask;
    search_order->sequential_trials_per_turn = sequential_trials_per_turn;
    search_order->sequential_trials_remaining = sequential_trials_per_turn;

    /*
     * Este cálculo permite deshacer el reordenamiento. Cada repetición duplica
     * la cantidad de bits correctos; seis repeticiones cubren los 64 bits del
     * número.
     */
    search_order->inverse_permutation_multiplier = 1;
    for (int iteration = 0; iteration < 6; iteration++) {
        search_order->inverse_permutation_multiplier *=
            2 - search_order->permutation_multiplier * search_order->inverse_permutation_multiplier;
    }
    return 1;
}

static uint64_t permute_once(const HybridOrder *search_order, uint64_t position)
{
    return (search_order->permutation_multiplier * position +
            search_order->permutation_shift) & search_order->permutation_mask;
}

static uint64_t invert_once(const HybridOrder *search_order, uint64_t position)
{
    return ((position - search_order->permutation_shift) *
            search_order->inverse_permutation_multiplier) & search_order->permutation_mask;
}

uint64_t hybrid_order_permute(const HybridOrder *search_order, uint64_t permutation_index)
{
    do {
        permutation_index = permute_once(search_order, permutation_index);
    } while (permutation_index >= search_order->block_candidate_count);
    return permutation_index;
}

uint64_t hybrid_order_inverse(const HybridOrder *search_order, uint64_t candidate_position)
{
    do {
        candidate_position = invert_once(search_order, candidate_position);
    } while (candidate_position >= search_order->block_candidate_count);
    return candidate_position;
}

HybridOrderStatus hybrid_order_next(HybridOrder *search_order,
                                    uint64_t *calculations_remaining,
                                    uint64_t *selected_position,
                                    HybridCandidateSource *candidate_source)
{
    if (search_order->candidates_emitted == search_order->block_candidate_count) {
        return HYBRID_ORDER_FINISHED;
    }
    while (*calculations_remaining > 0) {
        if (!search_order->mapping_in_progress) {
            if (search_order->sequential_trials_remaining > 0 &&
                search_order->next_sequential_position < search_order->block_candidate_count) {
                search_order->pending_source = HYBRID_SEQUENTIAL;
                search_order->pending_position = search_order->next_sequential_position++;
            } else {
                search_order->sequential_trials_remaining = 0;
                if (search_order->next_permutation_index == search_order->block_candidate_count) {
                    return HYBRID_ORDER_FINISHED;
                }
                search_order->pending_source = HYBRID_PERMUTED;
                search_order->pending_position = search_order->next_permutation_index++;
            }
            search_order->mapped_position = search_order->pending_position;
            search_order->mapping_in_progress = 1;
        }

        (*calculations_remaining)--;
        search_order->mapped_position = search_order->pending_source == HYBRID_SEQUENTIAL
            ? invert_once(search_order, search_order->mapped_position)
            : permute_once(search_order, search_order->mapped_position);
        if (search_order->mapped_position >= search_order->block_candidate_count) {
            continue;
        }
        search_order->mapping_in_progress = 0;

        if (search_order->pending_source == HYBRID_SEQUENTIAL) {
            /* Saltar una clave repetida conserva las pruebas nuevas que faltan en el turno. */
            if (search_order->mapped_position < search_order->next_permutation_index) {
                continue;
            }
            search_order->sequential_trials_remaining--;
            *selected_position = search_order->pending_position;
        } else {
            if (search_order->mapped_position < search_order->next_sequential_position) {
                continue;
            }
            search_order->sequential_trials_remaining = search_order->sequential_trials_per_turn;
            *selected_position = search_order->mapped_position;
        }
        search_order->candidates_emitted++;
        *candidate_source = search_order->pending_source;
        return HYBRID_CANDIDATE_READY;
    }
    return HYBRID_ORDER_PAUSED;
}
