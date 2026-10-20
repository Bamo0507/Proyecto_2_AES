/*
 * Cada trabajador busca y avisa al encontrar una coincidencia o terminar sus
 * claves. El trabajador 0 reúne esos avisos y decide cuándo detener a todos.
 * Antes de reunir los resultados, se comprueba que todos los avisos hayan
 * terminado de enviarse y recibirse.
 */
#include "hybrid_search_mpi.h"

#include <stdlib.h>

enum { HYBRID_REPORT_TAG = 31, HYBRID_STOP_TAG = 32 };
/* El límite incluye los cálculos para descartar claves repetidas o volver al grupo. */
#define HYBRID_CALCULATIONS_PER_CHECK UINT64_C(256)

typedef struct {
    HybridOrder search_order;
    uint64_t first_candidate;
    uint64_t found_candidate;
    uint64_t sequential_candidates;
    uint64_t permuted_candidates;
    int finished;
} LocalHybridSearch;

static void search_local_chunk(AesCtrContext *aes_context, const FileSearchProblem *search_problem,
                               LocalHybridSearch *local_search, int process_rank)
{
    uint64_t calculations_remaining = HYBRID_CALCULATIONS_PER_CHECK;
    while (calculations_remaining > 0 && !local_search->finished) {
        uint64_t candidate_position;
        HybridCandidateSource candidate_source;
        HybridOrderStatus order_status = hybrid_order_next(
            &local_search->search_order, &calculations_remaining,
            &candidate_position, &candidate_source);
        if (order_status == HYBRID_ORDER_PAUSED) {
            break;
        }
        if (order_status == HYBRID_ORDER_FINISHED) {
            local_search->finished = 1;
            break;
        }
        /* El recorrido devuelve una posición dentro del grupo; aquí se obtiene la clave completa. */
        uint64_t candidate = local_search->first_candidate + candidate_position;
        int matches_phrase;
        if (!file_search_test_candidate(aes_context, search_problem, candidate, &matches_phrase)) {
            mpi_file_search_abort("Error al verificar una candidata hibrida.", process_rank);
        }
        if (candidate_source == HYBRID_SEQUENTIAL) {
            local_search->sequential_candidates++;
        } else {
            local_search->permuted_candidates++;
        }
        if (matches_phrase) {
            local_search->found_candidate = candidate;
            local_search->finished = 1;
        } else if (local_search->search_order.candidates_emitted ==
                   local_search->search_order.block_candidate_count) {
            local_search->finished = 1;
        }
    }
}

static uint64_t search_as_worker(AesCtrContext *aes_context, const FileSearchProblem *search_problem,
                                 LocalHybridSearch *local_search, int process_rank,
                                 MPI_Request *stop_request, uint64_t *stop_candidate)
{
    MPI_Request report_request = MPI_REQUEST_NULL;
    int stop_received = 0;
    while (!local_search->finished) {
        MPI_Test(stop_request, &stop_received, MPI_STATUS_IGNORE);
        if (stop_received) {
            break;
        }
        search_local_chunk(aes_context, search_problem, local_search, process_rank);
    }
    /* Cada trabajador informa una vez y espera la decisión, aunque no le tocaran claves. */
    MPI_Isend(&local_search->found_candidate, 1, MPI_UINT64_T, 0, HYBRID_REPORT_TAG,
               MPI_COMM_WORLD, &report_request);
    if (!stop_received) {
        MPI_Wait(stop_request, MPI_STATUS_IGNORE);
    }
    MPI_Wait(&report_request, MPI_STATUS_IGNORE);
    return *stop_candidate;
}

static uint64_t search_as_coordinator(AesCtrContext *aes_context,
                                      const FileSearchProblem *search_problem,
                                      LocalHybridSearch *local_search, int process_count,
                                      MPI_Request *stop_requests)
{
    uint64_t selected_candidate = AES_CANDIDATE_NOT_FOUND;
    int reports_received = 0;
    while (!local_search->finished || reports_received < process_count - 1) {
        int report_available = 0;
        MPI_Status report_status;
        MPI_Iprobe(MPI_ANY_SOURCE, HYBRID_REPORT_TAG, MPI_COMM_WORLD,
                    &report_available, &report_status);
        if (report_available) {
            uint64_t reported_candidate;
            MPI_Recv(&reported_candidate, 1, MPI_UINT64_T, report_status.MPI_SOURCE,
                      HYBRID_REPORT_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            reports_received++;
            if (reported_candidate != AES_CANDIDATE_NOT_FOUND) {
                selected_candidate = reported_candidate;
                break;
            }
        }
        if (!local_search->finished) {
            search_local_chunk(aes_context, search_problem, local_search, 0);
        }
        if (local_search->found_candidate != AES_CANDIDATE_NOT_FOUND) {
            selected_candidate = local_search->found_candidate;
            break;
        }
    }

    for (int destination_rank = 1; destination_rank < process_count; destination_rank++) {
        MPI_Isend(&selected_candidate, 1, MPI_UINT64_T, destination_rank, HYBRID_STOP_TAG,
                   MPI_COMM_WORLD, &stop_requests[destination_rank - 1]);
    }
    /*
     * Puede quedar un aviso en camino cuando se ordena parar. Recibir los que
     * faltan permite que sus remitentes terminen de enviarlos.
     */
    while (reports_received < process_count - 1) {
        uint64_t ignored_candidate;
        MPI_Recv(&ignored_candidate, 1, MPI_UINT64_T, MPI_ANY_SOURCE,
                  HYBRID_REPORT_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        reports_received++;
    }
    if (process_count > 1) {
        MPI_Waitall(process_count - 1, stop_requests, MPI_STATUSES_IGNORE);
    }
    return selected_candidate;
}

HybridSearchResult mpi_hybrid_search(AesCtrContext *aes_context,
                                     const FileSearchOptions *search_options,
                                     const FileSearchProblem *search_problem,
                                     uint64_t sequential_trials_per_turn, uint64_t seed,
                                     int process_rank, int process_count)
{
    uint64_t first_candidate;
    uint64_t candidate_limit;
    file_search_block_bounds(search_options->total_candidates, process_count, process_rank,
                               &first_candidate, &candidate_limit);
    LocalHybridSearch local_search = {0};
    local_search.first_candidate = first_candidate;
    local_search.found_candidate = AES_CANDIDATE_NOT_FOUND;
    local_search.finished = first_candidate == candidate_limit;
    uint64_t worker_seed = seed + UINT64_C(0x9e3779b97f4a7c15) * (uint64_t)process_rank;
    if (!hybrid_order_initialize(&local_search.search_order, candidate_limit - first_candidate,
                                  sequential_trials_per_turn, worker_seed)) {
        mpi_file_search_abort("Parametros invalidos del recorrido hibrido.", process_rank);
    }

    uint64_t stop_candidate = AES_CANDIDATE_NOT_FOUND;
    MPI_Request stop_request = MPI_REQUEST_NULL;
    MPI_Request *stop_requests = NULL;
    if (process_rank != 0) {
        MPI_Irecv(&stop_candidate, 1, MPI_UINT64_T, 0, HYBRID_STOP_TAG,
                   MPI_COMM_WORLD, &stop_request);
    } else if (process_count > 1) {
        stop_requests = calloc((size_t)(process_count - 1), sizeof(*stop_requests));
        if (stop_requests == NULL) {
            mpi_file_search_abort("No se pudieron reservar requests de parada.", process_rank);
        }
    }

    HybridSearchResult search_result = {{AES_CANDIDATE_NOT_FOUND, 0, 0.0, -1}, 0, 0};
    MPI_Barrier(MPI_COMM_WORLD);
    double start_seconds = MPI_Wtime();
    search_result.summary.found_candidate = process_rank == 0
        ? search_as_coordinator(aes_context, search_problem, &local_search, process_count, stop_requests)
        : search_as_worker(aes_context, search_problem, &local_search, process_rank,
                             &stop_request, &stop_candidate);
    double local_elapsed_seconds = MPI_Wtime() - start_seconds;
    free(stop_requests);

    uint64_t local_candidate_counts[2] = {
        local_search.sequential_candidates, local_search.permuted_candidates
    };
    uint64_t total_candidate_counts[2] = {0, 0};
    MPI_Reduce(local_candidate_counts, total_candidate_counts, 2, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_elapsed_seconds, &search_result.summary.elapsed_seconds, 1,
               MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    search_result.sequential_candidates = total_candidate_counts[0];
    search_result.permuted_candidates = total_candidate_counts[1];
    search_result.summary.candidates_tested = total_candidate_counts[0] + total_candidate_counts[1];
    if (search_result.summary.found_candidate != AES_CANDIDATE_NOT_FOUND) {
        uint64_t block_candidate_count = (search_options->total_candidates + (uint64_t)process_count - 1) /
                              (uint64_t)process_count;
        search_result.summary.winning_rank = (int)(search_result.summary.found_candidate / block_candidate_count);
    }
    return search_result;
}
