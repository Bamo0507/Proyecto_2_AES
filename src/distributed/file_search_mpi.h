/*
 * Prepara y comparte el mensaje antes de que los trabajadores empiecen a
 * buscar.
 */
#ifndef AES_FILE_SEARCH_MPI_H
#define AES_FILE_SEARCH_MPI_H

#include "file_search.h"
#include <mpi.h>

/*
 * Prepara OpenSSL para cada trabajador. El trabajador 0 lee y cifra el mensaje;
 * después comparte los datos con los demás. Todos deben llamar a esta función
 * con las mismas opciones y en el mismo orden. aes_context debe crearse con
 * {0}.
 *
 * Devuelve 1 si el mensaje está listo y 0 para todos si falla su preparación.
 * Cada trabajador debe cerrar aes_context también en ese caso. Si no se pueden
 * preparar los datos de OpenSSL, se detiene todo el programa.
 */
int mpi_file_search_prepare(AesCtrContext *aes_context, const FileSearchOptions *search_options,
                            FileSearchProblem *search_problem, int process_rank);

/* Muestra el error y detiene a todos para que nadie quede esperando un aviso. */
_Noreturn void mpi_file_search_abort(const char *description, int process_rank);

#endif
