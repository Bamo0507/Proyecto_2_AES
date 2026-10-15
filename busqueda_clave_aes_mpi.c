/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3169 - Computacion Paralela y Distribuida
 * Miembros:    Adriana Palacios
 *              Bryan Martinez
 *              Daniela Ramirez
 * Ejercicio:   Busqueda paralela de una clave AES mediante fuerza bruta
 * Descripcion: lee un texto desde un archivo, lo cifra y descifra con
 *              AES-128 usando la clave indicada. Luego los procesos MPI
 *              buscan la clave por fuerza bruta, cada uno en un bloque
 *              contiguo de candidatas, hasta que el texto descifrado
 *              contenga la frase de busqueda.
 *
 *              Uso: mpirun -np n ./busqueda_clave_aes_mpi
 *                       <archivo.txt> <clave> [bits]
 *----------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <mpi.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#define KEY_LEN 16
#define IV_LEN 16
#define MAX_MESSAGE_LEN 4096

#define SEARCH_PHRASE "es una prueba de"

#define MIN_BITS 1
#define MAX_BITS 56
#define DEFAULT_BITS 56

/* Candidatas que prueba cada proceso antes de sincronizarse. */
#define CHECK_INTERVAL 4096

/* Muestra el error y aborta todos los procesos. */
static void fail(const char *description)
{
    fprintf(stderr, "%s\n", description);
    ERR_print_errors_fp(stderr);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
}

/* Convierte un argumento a entero sin signo; devuelve 0 si no es valido. */
static int parse_u64(const char *text, uint64_t *value)
{
    char *parse_end;

    errno = 0;
    unsigned long long parsed = strtoull(text, &parse_end, 10);

    if (errno != 0 || *parse_end != '\0' || text[0] == '-' || text[0] == '\0') {
        return 0;
    }

    *value = (uint64_t)parsed;
    return 1;
}

/* Devuelve 1 si la frase aparece dentro del texto. */
static int contains_phrase(const unsigned char *text, int text_len,
                           const char *phrase, int phrase_len)
{
    for (int start = 0; start + phrase_len <= text_len; start++) {
        if (memcmp(text + start, phrase, (size_t)phrase_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Lleva el numero a 128 bits: ceros a la izquierda, big-endian. */
static void make_key(uint64_t number, unsigned char key[KEY_LEN])
{
    memset(key, 0, KEY_LEN);

    for (int byte_index = 0; byte_index < 8; byte_index++) {
        key[KEY_LEN - 1 - byte_index] = (unsigned char)(number >> (8 * byte_index));
    }
}

/*
 * Lee el archivo en buffer (de tamano MAX_MESSAGE_LEN + 1).
 * Devuelve la longitud del texto, o -1 si hay error.
 */
static int read_message(const char *path, unsigned char *buffer)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        perror("No se pudo abrir el archivo");
        return -1;
    }

    size_t message_len = fread(buffer, 1, MAX_MESSAGE_LEN + 1, file);
    int read_error = ferror(file);
    fclose(file);

    if (read_error) {
        fprintf(stderr, "Error al leer el archivo.\n");
        return -1;
    }

    if (message_len > MAX_MESSAGE_LEN) {
        fprintf(stderr, "El archivo excede %d bytes.\n", MAX_MESSAGE_LEN);
        return -1;
    }

    /* Ignora saltos de linea finales. */
    while (message_len > 0 &&
           (buffer[message_len - 1] == '\n' || buffer[message_len - 1] == '\r')) {
        message_len--;
    }

    if (message_len == 0) {
        fprintf(stderr, "El archivo esta vacio.\n");
        return -1;
    }

    return (int)message_len;
}

/* AES-128-CTR: cifrar y descifrar son la misma operacion. */
static void crypt_ctr(
    EVP_CIPHER_CTX *cipher_ctx,
    const EVP_CIPHER *cipher,
    const unsigned char key[KEY_LEN],
    const unsigned char iv[IV_LEN],
    const unsigned char *input,
    unsigned char *output,
    int input_len)
{
    int written = 0;
    int final_written = 0;

    if (EVP_CipherInit_ex(cipher_ctx, cipher, NULL, key, iv, 1) != 1) {
        fail("Error al inicializar AES.");
    }

    if (EVP_CipherUpdate(cipher_ctx, output, &written, input, input_len) != 1) {
        fail("Error al procesar el mensaje.");
    }

    if (EVP_CipherFinal_ex(cipher_ctx, output + written, &final_written) != 1) {
        fail("Error al finalizar la operacion AES.");
    }

    if (written + final_written != input_len) {
        fail("Longitud inesperada del resultado.");
    }
}

static void print_hex(const unsigned char *data, int data_len)
{
    for (int byte_index = 0; byte_index < data_len; byte_index++) {
        printf("%02x", data[byte_index]);
    }
    putchar('\n');
}

int main(int argc, char *argv[])
{
    int rank;
    int num_processes;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_processes);

    /* Todos validan los mismos argumentos y llegan a la misma conclusion. */
    uint64_t bits_arg = DEFAULT_BITS;
    uint64_t secret = 0;
    int valid = 1;

    if (argc < 3 || argc > 4) {
        valid = 0;
        if (rank == 0) {
            fprintf(stderr, "Uso: mpirun -np n %s <archivo.txt> <clave> [bits]\n",
                    argv[0]);
        }
    } else if (!parse_u64(argv[2], &secret)) {
        valid = 0;
        if (rank == 0) {
            fprintf(stderr, "Valor invalido para clave: %s\n", argv[2]);
        }
    } else if (argc > 3 && !parse_u64(argv[3], &bits_arg)) {
        valid = 0;
        if (rank == 0) {
            fprintf(stderr, "Valor invalido para bits: %s\n", argv[3]);
        }
    } else if (bits_arg < MIN_BITS || bits_arg > MAX_BITS) {
        valid = 0;
        if (rank == 0) {
            fprintf(stderr, "bits debe estar entre %d y %d.\n", MIN_BITS, MAX_BITS);
        }
    }

    if (!valid) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    int bits = (int)bits_arg;
    uint64_t total_keys = UINT64_C(1) << bits;

    if (secret >= total_keys) {
        if (rank == 0) {
            fprintf(stderr, "clave debe ser menor que 2^%d.\n", bits);
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    unsigned char message[MAX_MESSAGE_LEN + 1];
    int message_len = 0;
    int phrase_len = (int)strlen(SEARCH_PHRASE);

    /* Solo el rank 0 lee el archivo; los demas sabran si fallo. */
    if (rank == 0) {
        message_len = read_message(argv[1], message);

        if (message_len > 0 &&
            !contains_phrase(message, message_len, SEARCH_PHRASE, phrase_len)) {
            fprintf(stderr, "El texto no contiene la frase \"%s\".\n", SEARCH_PHRASE);
            message_len = -1;
        }
    }

    MPI_Bcast(&message_len, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (message_len <= 0) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    EVP_CIPHER *cipher = EVP_CIPHER_fetch(NULL, "AES-128-CTR", NULL);
    EVP_CIPHER_CTX *cipher_ctx = EVP_CIPHER_CTX_new();

    if (cipher == NULL || cipher_ctx == NULL) {
        fail("No se pudo preparar OpenSSL.");
    }

    unsigned char iv[IV_LEN];
    unsigned char key[KEY_LEN];
    unsigned char cipher_text[MAX_MESSAGE_LEN];
    unsigned char plain_text[MAX_MESSAGE_LEN];

    /* Solo el rank 0 cifra y descifra: el IV aleatorio debe ser uno solo. */
    if (rank == 0) {
        if (RAND_bytes(iv, IV_LEN) != 1) {
            fail("No se pudieron generar datos aleatorios.");
        }

        make_key(secret, key);
        crypt_ctr(cipher_ctx, cipher, key, iv, message, cipher_text, message_len);
        crypt_ctr(cipher_ctx, cipher, key, iv, cipher_text, plain_text, message_len);

        printf("Clave:            %" PRIu64 "\n", secret);
        printf("Clave AES-128:    ");
        print_hex(key, KEY_LEN);
        printf("Texto original:   ");
        fwrite(message, 1, (size_t)message_len, stdout);
        putchar('\n');
        printf("Texto cifrado:    ");
        print_hex(cipher_text, message_len);
        printf("Texto recuperado: ");
        fwrite(plain_text, 1, (size_t)message_len, stdout);
        printf("\n\n");
    }

    /* La busqueda solo conoce el IV y el texto cifrado, no la clave. */
    MPI_Bcast(iv, IV_LEN, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    MPI_Bcast(cipher_text, message_len, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

    /* Bloque contiguo de cada proceso: [block_start, block_end). */
    uint64_t block_size = (total_keys + (uint64_t)num_processes - 1) / (uint64_t)num_processes;
    uint64_t block_start = block_size * (uint64_t)rank;
    uint64_t block_end = block_start + block_size;

    if (block_start > total_keys) {
        block_start = total_keys;
    }
    if (block_end > total_keys) {
        block_end = total_keys;
    }

    uint64_t local_found_candidate = UINT64_MAX;
    uint64_t global_found_candidate = UINT64_MAX;
    uint64_t local_candidates_tested = 0;
    uint64_t total_candidates_tested = 0;

    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();

    /* Todos hacen el mismo numero de rondas para coincidir en el Allreduce. */
    for (uint64_t offset = 0; offset < block_size; offset += CHECK_INTERVAL) {
        uint64_t round_start = block_start + offset;
        uint64_t round_end = round_start + CHECK_INTERVAL;

        if (round_start > block_end) {
            round_start = block_end;
        }
        if (round_end > block_end) {
            round_end = block_end;
        }

        for (uint64_t candidate = round_start; candidate < round_end; candidate++) {
            make_key(candidate, key);
            crypt_ctr(cipher_ctx, cipher, key, iv, cipher_text, plain_text, message_len);
            local_candidates_tested++;

            if (contains_phrase(plain_text, message_len, SEARCH_PHRASE, phrase_len)) {
                local_found_candidate = candidate;
                break;
            }
        }

        /* Si algun proceso encontro la clave, todos se detienen. */
        MPI_Allreduce(&local_found_candidate, &global_found_candidate, 1,
                      MPI_UINT64_T, MPI_MIN, MPI_COMM_WORLD);

        if (global_found_candidate != UINT64_MAX) {
            break;
        }
    }

    double local_elapsed_seconds = MPI_Wtime() - start_time;
    double elapsed_seconds = 0.0;

    /* El tiempo de la ejecucion es el del proceso que tardo mas. */
    MPI_Reduce(&local_elapsed_seconds, &elapsed_seconds, 1,
               MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&local_candidates_tested, &total_candidates_tested, 1,
               MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("Ejecucion: paralela con %d procesos (bloques)\n", num_processes);
        printf("Rango explorado: 2^%d candidatas\n", bits);
        printf("Frase de busqueda: \"%s\"\n", SEARCH_PHRASE);

        if (global_found_candidate != UINT64_MAX) {
            make_key(global_found_candidate, key);
            crypt_ctr(cipher_ctx, cipher, key, iv, cipher_text, plain_text, message_len);

            printf("Clave encontrada: candidata %" PRIu64
                   " (proceso %" PRIu64 ")\n",
                   global_found_candidate,
                   global_found_candidate / block_size);
            printf("Clave AES-128: ");
            print_hex(key, KEY_LEN);
            printf("Mensaje: ");
            fwrite(plain_text, 1, (size_t)message_len, stdout);
            putchar('\n');
        } else {
            printf("No se encontro la clave.\n");
        }

        printf("Candidatas probadas: %" PRIu64 "\n", total_candidates_tested);
        printf("Tiempo: %.6f segundos\n", elapsed_seconds);

        if (elapsed_seconds > 0.0) {
            printf("Rendimiento: %.0f candidatas/segundo\n",
                   (double)total_candidates_tested / elapsed_seconds);
        }
    }

    EVP_CIPHER_CTX_free(cipher_ctx);
    EVP_CIPHER_free(cipher);
    MPI_Finalize();

    return EXIT_SUCCESS;
}
