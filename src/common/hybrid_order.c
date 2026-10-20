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
 * * Este cálculo permite deshacer el reordenamiento. Cada repetición duplica *
 * la cantidad de bits correctos; seis repeticiones cubren los 64 bits del *
 * número.
 */
    search_order->inverse_permutation_multiplier = 1;
    for (int iteration = 0; iteration < 6; iteration++) {
        search_order->inverse_permutation_multiplier *=
            2 - search_order->permutation_multiplier * search_order->inverse_permutation_multiplier;
    }
    return 1;
}

uint64_t hybrid_order_permute(const HybridOrder *search_order, uint64_t permutation_index)
{
    do {
        permutation_index = (search_order->permutation_multiplier * permutation_index +
                             search_order->permutation_shift) & search_order->permutation_mask;
    } while (permutation_index >= search_order->block_candidate_count);
    return permutation_index;
}

uint64_t hybrid_order_inverse(const HybridOrder *search_order, uint64_t candidate_position)
{
    do {
        candidate_position = ((candidate_position - search_order->permutation_shift) *
                              search_order->inverse_permutation_multiplier) & search_order->permutation_mask;
    } while (candidate_position >= search_order->block_candidate_count);
    return candidate_position;
}

int hybrid_order_next(HybridOrder *search_order, uint64_t *selected_position,
                      HybridCandidateSource *candidate_source)
{
    if (search_order->candidates_emitted == search_order->block_candidate_count) {
        return 0;
    }
    if (search_order->sequential_trials_remaining > 0) {
        while (search_order->next_sequential_position < search_order->block_candidate_count) {
            uint64_t candidate_position = search_order->next_sequential_position++;
            /* Solo se cuenta la prueba si el otro recorrido todavía no pasó por esta clave. */
            if (hybrid_order_inverse(search_order, candidate_position) >= search_order->next_permutation_index) {
                search_order->sequential_trials_remaining--;
                search_order->candidates_emitted++;
                *selected_position = candidate_position;
                *candidate_source = HYBRID_SEQUENTIAL;
                return 1;
            }
        }
        search_order->sequential_trials_remaining = 0;
    }
    while (search_order->next_permutation_index < search_order->block_candidate_count) {
        uint64_t candidate_position =
            hybrid_order_permute(search_order, search_order->next_permutation_index++);
        /* Las posiciones anteriores a este punto ya las cubrió el recorrido consecutivo. */
        if (candidate_position >= search_order->next_sequential_position) {
            search_order->sequential_trials_remaining = search_order->sequential_trials_per_turn;
            search_order->candidates_emitted++;
            *selected_position = candidate_position;
            *candidate_source = HYBRID_PERMUTED;
            return 1;
        }
    }
    return 0;
}
