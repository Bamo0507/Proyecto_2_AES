/*
 * Comprueba que todos los trabajadores terminen, también cuando no hay una
 * clave correcta entre las que buscan.
 */
#include "hybrid_search_mpi.h"

#include <stdlib.h>

#define CHECK_MPI(condition) do { \
    if (!(condition)) { \
        mpi_file_search_abort("Fallo en prueba de terminacion hibrida: " #condition, process_rank); \
    } \
} while (0)

int main(int argument_count, char *arguments[])
{
    MPI_Init(&argument_count, &arguments);
    int process_rank;
    int process_count;
    MPI_Comm_rank(MPI_COMM_WORLD, &process_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &process_count);
    AesCtrContext aes_context = {0};
    CHECK_MPI(aes_ctr_context_open(&aes_context));
    const uint64_t k_values[] = {8, 16, 32};
    for (size_t k_index = 0; k_index < sizeof(k_values) / sizeof(k_values[0]); k_index++) {
        /*
         * Los casos prueban, en este orden: la primera clave, la última,
         * ninguna coincidencia y ninguna coincidencia con solo dos claves.
         * El cuarto permite dejar trabajadores sin claves asignadas.
         * Un quinto caso usa más claves y semilla 0 para recorrer tramos
         * repetidos que requieren pausar antes de seguir buscando.
         */
        for (int case_index = 0; case_index < 5; case_index++) {
            int unknown_key_bits = case_index == 3 ? 1 : (case_index == 4 ? 14 : 8);
            FileSearchOptions search_options = {"fixture", 0, unknown_key_bits,
                                                UINT64_C(1) << unknown_key_bits};
            uint64_t encryption_candidate = case_index == 0 ? 0 :
                (case_index == 1 ? search_options.total_candidates - 1 : search_options.total_candidates);
            FileSearchProblem search_problem = {0};
            unsigned char key[AES_KEY_LENGTH];
            file_search_make_key(encryption_candidate, key);
            search_problem.message_length = sizeof(FILE_SEARCH_PHRASE) - 1;
            CHECK_MPI(aes_ctr_transform_buffer(&aes_context, key, search_problem.initialization_vector,
                         (const unsigned char *)FILE_SEARCH_PHRASE, search_problem.message_length,
                         search_problem.ciphertext, sizeof(search_problem.ciphertext)));
            /* Usar una clave fuera del conjunto obliga a terminar todas las pruebas sin encontrarla. */
            HybridSearchResult search_result = mpi_hybrid_search(&aes_context, &search_options, &search_problem,
                                                            k_values[k_index], case_index == 4 ? 0 : 42,
                                                            process_rank, process_count);
            uint64_t expected_candidate = case_index < 2
                ? encryption_candidate : AES_CANDIDATE_NOT_FOUND;
            CHECK_MPI(search_result.summary.found_candidate == expected_candidate);
            if (process_rank == 0) {
                CHECK_MPI(search_result.summary.candidates_tested > 0);
                CHECK_MPI(search_result.summary.candidates_tested <= search_options.total_candidates);
                CHECK_MPI(search_result.summary.candidates_tested ==
                             search_result.sequential_candidates + search_result.permuted_candidates);
                if (case_index >= 2) {
                    CHECK_MPI(search_result.summary.candidates_tested == search_options.total_candidates);
                    CHECK_MPI(search_result.summary.winning_rank == -1);
                }
            }
        }
    }
    if (process_rank == 0) {
        puts("OK: notificaciones, solicitudes completas y agotamiento global sin duplicados.");
    }
    aes_ctr_context_close(&aes_context);
    MPI_Finalize();
    return EXIT_SUCCESS;
}
