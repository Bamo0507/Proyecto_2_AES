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
                    CHECK(hybrid_order_next(&search_order, &candidate_position, &candidate_source));
                    CHECK(hybrid_order_next(&repeated_order, &repeated_position, &repeated_source));
                    CHECK(candidate_position == repeated_position && candidate_source == repeated_source);
                    CHECK(candidate_position < block_candidate_count && !visited_positions[candidate_position]);
                    visited_positions[candidate_position] = 1;
                    if (selected_count < sequential_trial_counts[trial_count_index]) {
                        CHECK(candidate_source == HYBRID_SEQUENTIAL && candidate_position == selected_count);
                    }
                }
                uint64_t unused_position;
                HybridCandidateSource unused_candidate_source;
                CHECK(!hybrid_order_next(&search_order, &unused_position, &unused_candidate_source));
                CHECK(search_order.candidates_emitted == block_candidate_count);
                for (uint64_t candidate_position = 0; candidate_position < block_candidate_count;
                     candidate_position++) {
                    CHECK(visited_positions[candidate_position]);
                }
            }
        }
    }
    HybridOrder search_order;
    CHECK(!hybrid_order_initialize(&search_order, 1, 0, 1));
    CHECK(!hybrid_order_initialize(&search_order, HYBRID_MAX_BLOCK_SIZE + 1, 8, 1));
    CHECK(hybrid_order_initialize(&search_order, HYBRID_MAX_BLOCK_SIZE, 8, UINT64_MAX));
    for (uint64_t permutation_index = 0; permutation_index < 1000; permutation_index++) {
        CHECK(hybrid_order_inverse(&search_order,
                  hybrid_order_permute(&search_order, permutation_index)) == permutation_index);
    }

    /*
 * * El reparto debe cubrir todas las claves sin repetirlas, incluso si algunos
 * * trabajadores no reciben ninguna.
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
    puts("OK: permutacion/inversa, cobertura sin duplicados, k=8/16/32, bloques y CTR de 4096 bytes.");
    return EXIT_SUCCESS;
}
