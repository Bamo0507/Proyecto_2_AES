/*
 * Prueba todos los casos de grupos pequeños para comprobar que ninguna clave se
 * repita o quede sin visitar.
 */
#include "file_search.h"
#include "hybrid_order.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Fallo en linea %d: %s\n", __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

static HybridOrderStatus next_after_pauses(HybridOrder *search_order,
                                          uint64_t calculations_per_call,
                                          uint64_t *selected_position,
                                          HybridCandidateSource *candidate_source)
{
    HybridOrderStatus order_status;
    do {
        uint64_t calculations_remaining = calculations_per_call;
        order_status = hybrid_order_next(search_order, &calculations_remaining,
                                          selected_position, candidate_source);
    } while (order_status == HYBRID_ORDER_PAUSED);
    return order_status;
}

static int check_mapping_pauses(void)
{
    int paused_while_permuting = 0;
    int paused_while_inverting = 0;
    const uint64_t trial_counts[] = {1, UINT64_MAX};
    /* Cinco claves usan un cálculo de ocho posiciones; algunas quedan fuera del grupo. */
    for (size_t trial_index = 0; trial_index < sizeof(trial_counts) / sizeof(trial_counts[0]);
         trial_index++) {
        HybridOrder search_order;
        CHECK(hybrid_order_initialize(&search_order, 5, trial_counts[trial_index], 0));
        unsigned char visited_positions[5] = {0};
        for (;;) {
            uint64_t selected_position = UINT64_MAX;
            HybridCandidateSource candidate_source = HYBRID_PERMUTED;
            uint64_t previous_count = search_order.candidates_emitted;
            uint64_t calculations_remaining = 1;
            HybridOrderStatus order_status = hybrid_order_next(
                &search_order, &calculations_remaining, &selected_position, &candidate_source);
            if (order_status == HYBRID_ORDER_FINISHED) {
                break;
            }
            CHECK(calculations_remaining == 0);
            if (order_status == HYBRID_ORDER_PAUSED) {
                CHECK(selected_position == UINT64_MAX && candidate_source == HYBRID_PERMUTED);
                CHECK(search_order.candidates_emitted == previous_count);
                if (search_order.mapping_in_progress) {
                    CHECK(search_order.mapped_position >= search_order.block_candidate_count);
                    if (search_order.pending_source == HYBRID_SEQUENTIAL) {
                        paused_while_inverting = 1;
                    } else {
                        paused_while_permuting = 1;
                    }
                }
                continue;
            }
            CHECK(order_status == HYBRID_CANDIDATE_READY);
            CHECK(selected_position < 5 && !visited_positions[selected_position]);
            visited_positions[selected_position] = 1;
        }
        CHECK(search_order.candidates_emitted == 5);
    }
    CHECK(paused_while_permuting && paused_while_inverting);
    return EXIT_SUCCESS;
}

static int check_long_skip_pauses(void)
{
    HybridOrder search_order;
    CHECK(hybrid_order_initialize(&search_order, UINT64_C(1) << 20, 8, 0));
    uint64_t calculations_remaining = 0;
    uint64_t selected_position = UINT64_MAX;
    HybridCandidateSource candidate_source = HYBRID_PERMUTED;
    CHECK(hybrid_order_next(&search_order, &calculations_remaining,
                             &selected_position, &candidate_source) == HYBRID_ORDER_PAUSED);
    CHECK(search_order.next_sequential_position == 0 && search_order.next_permutation_index == 0);
    CHECK(selected_position == UINT64_MAX && candidate_source == HYBRID_PERMUTED);

    /* Con esta semilla, el recorrido revuelto visita un tramo consecutivo más adelante. */
    uint64_t repeated_start = search_order.permutation_shift;
    uint64_t repeated_count = repeated_start / 8;
    uint64_t prefix_count = repeated_start + repeated_count;
    CHECK(prefix_count < search_order.block_candidate_count && repeated_count > 256);
    while (search_order.candidates_emitted < prefix_count) {
        CHECK(next_after_pauses(&search_order, UINT64_MAX,
                                &selected_position, &candidate_source) == HYBRID_CANDIDATE_READY);
    }
    CHECK(search_order.next_sequential_position == repeated_start);
    CHECK(search_order.next_permutation_index == repeated_count);

    int pause_count = 0;
    for (;;) {
        calculations_remaining = 256;
        uint64_t previous_position = search_order.next_sequential_position;
        HybridOrderStatus order_status = hybrid_order_next(
            &search_order, &calculations_remaining, &selected_position, &candidate_source);
        CHECK(search_order.next_sequential_position - previous_position <= 256);
        if (order_status == HYBRID_ORDER_PAUSED) {
            CHECK(calculations_remaining == 0 && search_order.candidates_emitted == prefix_count);
            pause_count++;
            continue;
        }
        CHECK(order_status == HYBRID_CANDIDATE_READY);
        break;
    }
    CHECK(pause_count > 0);
    CHECK(selected_position == prefix_count && candidate_source == HYBRID_SEQUENTIAL);
    CHECK(search_order.candidates_emitted == prefix_count + 1);
    return EXIT_SUCCESS;
}

int main(void)
{
    const uint64_t seeds[] = {0, 1, 42, UINT64_MAX};
    const uint64_t sequential_trial_counts[] = {1, 8, 16, 32, UINT64_MAX};
    for (uint64_t block_candidate_count = 0; block_candidate_count <= 257; block_candidate_count++) {
        for (size_t seed_index = 0; seed_index < sizeof(seeds) / sizeof(seeds[0]); seed_index++) {
            for (size_t trial_count_index = 0;
                 trial_count_index < sizeof(sequential_trial_counts) / sizeof(sequential_trial_counts[0]);
                 trial_count_index++) {
                HybridOrder search_order;
                HybridOrder repeated_order;
                unsigned char visited_positions[258] = {0};
                CHECK(hybrid_order_initialize(&search_order, block_candidate_count,
                                                sequential_trial_counts[trial_count_index], seeds[seed_index]));
                CHECK(hybrid_order_initialize(&repeated_order, block_candidate_count,
                                                sequential_trial_counts[trial_count_index], seeds[seed_index]));
                for (uint64_t permutation_index = 0; permutation_index < block_candidate_count;
                     permutation_index++) {
                    uint64_t permuted_position = hybrid_order_permute(&search_order, permutation_index);
                    CHECK(permuted_position < block_candidate_count);
                    CHECK(hybrid_order_inverse(&search_order, permuted_position) == permutation_index);
                }
                for (uint64_t selected_count = 0; selected_count < block_candidate_count; selected_count++) {
                    uint64_t candidate_position;
                    uint64_t repeated_position;
                    HybridCandidateSource candidate_source;
                    HybridCandidateSource repeated_source;
                    CHECK(next_after_pauses(&search_order, 1,
                                            &candidate_position, &candidate_source) == HYBRID_CANDIDATE_READY);
                    CHECK(next_after_pauses(&repeated_order, UINT64_MAX,
                                            &repeated_position, &repeated_source) == HYBRID_CANDIDATE_READY);
                    CHECK(candidate_position == repeated_position && candidate_source == repeated_source);
                    CHECK(candidate_position < block_candidate_count && !visited_positions[candidate_position]);
                    visited_positions[candidate_position] = 1;
                    if (selected_count < sequential_trial_counts[trial_count_index]) {
                        CHECK(candidate_source == HYBRID_SEQUENTIAL && candidate_position == selected_count);
                    }
                }
                uint64_t unused_position;
                HybridCandidateSource unused_candidate_source;
                CHECK(next_after_pauses(&search_order, 1,
                                        &unused_position, &unused_candidate_source) == HYBRID_ORDER_FINISHED);
                CHECK(search_order.candidates_emitted == block_candidate_count);
                for (uint64_t candidate_position = 0; candidate_position < block_candidate_count;
                     candidate_position++) {
                    CHECK(visited_positions[candidate_position]);
                }
            }
        }
    }
    CHECK(check_mapping_pauses() == EXIT_SUCCESS);
    CHECK(check_long_skip_pauses() == EXIT_SUCCESS);
    /* Orden esperado para 17 claves, k=2 y semilla 42. */
    const uint64_t expected_positions[] = {0, 1, 8, 2, 3, 12, 4, 5, 6, 7, 9, 15, 10, 11, 13, 14, 16};
    HybridOrder search_order;
    CHECK(hybrid_order_initialize(&search_order, 17, 2, 42));
    for (size_t position_index = 0;
         position_index < sizeof(expected_positions) / sizeof(expected_positions[0]); position_index++) {
        uint64_t selected_position;
        HybridCandidateSource candidate_source;
        CHECK(next_after_pauses(&search_order, 1,
                                &selected_position, &candidate_source) == HYBRID_CANDIDATE_READY);
        CHECK(selected_position == expected_positions[position_index]);
    }
    CHECK(!hybrid_order_initialize(&search_order, 1, 0, 1));
    CHECK(!hybrid_order_initialize(&search_order, HYBRID_MAX_BLOCK_SIZE + 1, 8, 1));
    CHECK(hybrid_order_initialize(&search_order, HYBRID_MAX_BLOCK_SIZE, 8, UINT64_MAX));
    for (uint64_t permutation_index = 0; permutation_index < 1000; permutation_index++) {
        CHECK(hybrid_order_inverse(&search_order,
                  hybrid_order_permute(&search_order, permutation_index)) == permutation_index);
    }

    /*
     * El reparto debe cubrir todas las claves sin repetirlas, incluso si
     * algunos trabajadores no reciben ninguna.
     */
    for (int process_count = 1; process_count <= 17; process_count++) {
        for (uint64_t total_candidates = 1; total_candidates <= 257; total_candidates++) {
            uint64_t previous_end = 0;
            for (int process_rank = 0; process_rank < process_count; process_rank++) {
                uint64_t first_candidate;
                uint64_t candidate_limit;
                file_search_block_bounds(total_candidates, process_count, process_rank,
                                           &first_candidate, &candidate_limit);
                CHECK(first_candidate == previous_end && first_candidate <= candidate_limit &&
                      candidate_limit <= total_candidates);
                previous_end = candidate_limit;
            }
            CHECK(previous_end == total_candidates);
        }
    }
    AesCtrContext aes_context = {0};
    FileSearchProblem search_problem = {0};
    unsigned char key[AES_KEY_LENGTH];
    unsigned char message[FILE_SEARCH_MAX_MESSAGE_LENGTH];
    unsigned char plaintext[FILE_SEARCH_OUTPUT_CAPACITY];
    memset(message, 'x', sizeof(message));
    memcpy(message + sizeof(message) - (sizeof(FILE_SEARCH_PHRASE) - 1),
           FILE_SEARCH_PHRASE, sizeof(FILE_SEARCH_PHRASE) - 1);
    message[0] = '\0'; /* La frase debe encontrarse también después de un byte que valga cero. */
    search_problem.message_length = sizeof(message);
    file_search_make_key(UINT64_C(0x123456789abcde), key);
    CHECK(key[0] == 0 && key[8] == 0 && key[9] == 0x12 && key[15] == 0xde);
    CHECK(aes_ctr_context_open(&aes_context));
    CHECK(!aes_ctr_transform_buffer(&aes_context, key, search_problem.initialization_vector,
                                      message, sizeof(message), search_problem.ciphertext,
                                      sizeof(message)));
    CHECK(aes_ctr_transform_buffer(&aes_context, key, search_problem.initialization_vector,
                                     message, sizeof(message), search_problem.ciphertext,
                                     sizeof(search_problem.ciphertext)));
    CHECK(file_search_decrypt_candidate(&aes_context, &search_problem,
                                          UINT64_C(0x123456789abcde), plaintext));
    CHECK(memcmp(message, plaintext, sizeof(message)) == 0);
    int matches_phrase;
    CHECK(file_search_test_candidate(&aes_context, &search_problem,
                                       UINT64_C(0x123456789abcde), &matches_phrase));
    CHECK(matches_phrase);
    CHECK(file_search_test_candidate(&aes_context, &search_problem, 0, &matches_phrase));
    CHECK(!matches_phrase);
    aes_ctr_context_close(&aes_context);
    puts("OK: recorrido con pausas, cobertura sin duplicados, k=8/16/32, bloques y CTR de 4096 bytes.");
    return EXIT_SUCCESS;
}
