/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3169 - Computacion Paralela y Distribuida
 * Miembros:    Adriana Palacios
 *              Bryan Martinez
 *              Daniela Ramirez
 * Ejercicio:   Busqueda paralela de una clave AES mediante fuerza bruta
 * Descripcion: los procesos MPI se reparten las claves candidatas y
 *              se detienen juntos al encontrar la clave o agotar el rango.
 *
 *              Uso: mpirun -np n ./busqueda_clave_aes_mpi
 *                       [bits] [clave_secreta] [mensaje]
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
#define MAX_MESSAGE_LEN 256

/* Bytes finales de la clave que se tratan como entero de 64 bits. */
#define TAIL_BYTES 8

/*
 * Bytes del mensaje que se descifran para verificar una candidata.
 * Con 8 bytes (64 bits), la probabilidad de que una clave incorrecta
 * coincida por azar es de 2^-64, despreciable para este ejercicio.
 */
#define PREFIX_LEN 8

/*
 * Rango permitido de bits desconocidos. Con 40 bits ya hay mas de un
 * billon de candidatas, suficiente para medir sin esperar dias.
 */
#define MIN_BITS 1
#define MAX_BITS 40

/* 2^24 (unos 16.7 millones de candidatas): unos segundos de busqueda. */
#define DEFAULT_BITS 24
#define DEFAULT_MESSAGE "Puedes lograrlo!"

/*
 * Candidatas que prueba cada proceso antes de sincronizarse. Un valor
 * mayor reduce la comunicacion, pero hace que los procesos tarden mas
 * en enterarse de que otro ya encontro la clave.
 */
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

/*
 * La clave usa los 16 bytes de una base aleatoria. Solo los ultimos
 * `bits` bits se reemplazan por la candidata: se simula que el atacante
 * conoce el resto de la clave y debe adivinar unicamente esa parte.
 */
static void make_key(
    const unsigned char base[KEY_LEN],
    uint64_t candidate,
    int bits,
    unsigned char key[KEY_LEN])
{
    uint64_t candidate_mask = (UINT64_C(1) << bits) - 1;
    uint64_t key_tail = 0;
    int tail_start = KEY_LEN - TAIL_BYTES;

    memcpy(key, base, KEY_LEN);

    /* Los ultimos 8 bytes como entero big-endian. */
    for (int byte_index = 0; byte_index < TAIL_BYTES; byte_index++) {
        key_tail = (key_tail << 8) | key[tail_start + byte_index];
    }

    key_tail = (key_tail & ~candidate_mask) | (candidate & candidate_mask);

    /* Se escribe de vuelta, del byte menos significativo al mas significativo. */
    for (int byte_index = TAIL_BYTES - 1; byte_index >= 0; byte_index--) {
        key[tail_start + byte_index] = (unsigned char)(key_tail & 0xFF);
        key_tail >>= 8;
    }
}

/*
 * AES-128-CTR: cifrar y descifrar son la misma operacion, por lo
 * que no hace falta indicar la direccion. El IV es publico: quien
 * descifra tambien lo conoce. CTR no requiere relleno.
 */
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

/* Imprime un arreglo de bytes en hexadecimal, seguido de un salto de linea. */
static void print_hex(const unsigned char *data, int data_len)
{
    for (int byte_index = 0; byte_index < data_len; byte_index++) {
        printf("%02x", data[byte_index]);
    }
    putchar('\n');
}

/*
 * Valida los argumentos, el rank 0 cifra el mensaje y lo comparte,
 * todos los procesos buscan en paralelo su parte de las candidatas
 * y el rank 0 reporta la clave, el tiempo y el rendimiento.
 */
int main(int argc, char *argv[])
{
    int rank;
    int num_processes;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_processes);

    /* Todos los procesos leen los mismos argumentos, por lo que
     * todos llegan a la misma conclusion sobre si son validos. */
    uint64_t bits_arg = DEFAULT_BITS;
    uint64_t secret = 0;
    const char *message = (argc > 3) ? argv[3] : DEFAULT_MESSAGE;
    int valid = 1;

    if (argc > 4) {
        valid = 0;
        if (rank == 0) {
            fprintf(stderr, "Uso: mpirun -np n %s [bits] [clave_secreta] [mensaje]\n",
                    argv[0]);
        }
    } else if (argc > 1 && !parse_u64(argv[1], &bits_arg)) {
        valid = 0;
        if (rank == 0) {
            fprintf(stderr, "Valor invalido para bits: %s\n", argv[1]);
        }
    } else if (bits_arg < MIN_BITS || bits_arg > MAX_BITS) {
        valid = 0;
        if (rank == 0) {
            fprintf(stderr, "bits debe estar entre %d y %d.\n", MIN_BITS, MAX_BITS);
        }
    } else if (argc > 2 && !parse_u64(argv[2], &secret)) {
        valid = 0;
        if (rank == 0) {
            fprintf(stderr, "Valor invalido para clave_secreta: %s\n", argv[2]);
        }
    } else if (strlen(message) == 0 || strlen(message) > MAX_MESSAGE_LEN) {
        valid = 0;
        if (rank == 0) {
            fprintf(stderr, "El mensaje debe tener entre 1 y %d bytes.\n",
                    MAX_MESSAGE_LEN);
        }
    }

    if (!valid) {
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    int bits = (int)bits_arg;
    uint64_t total_keys = UINT64_C(1) << bits;

    /* Por defecto se usa el peor caso: la ultima candidata. */
    if (argc <= 2) {
        secret = total_keys - 1;
    } else if (secret >= total_keys) {
        if (rank == 0) {
            fprintf(stderr, "clave_secreta debe ser menor que 2^%d.\n", bits);
        }
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    int message_len = (int)strlen(message);
    int prefix_len = (message_len < PREFIX_LEN) ? message_len : PREFIX_LEN;

    /* Cifrador obtenido una sola vez por proceso. */
    EVP_CIPHER *cipher = EVP_CIPHER_fetch(NULL, "AES-128-CTR", NULL);
    EVP_CIPHER_CTX *cipher_ctx = EVP_CIPHER_CTX_new();

    if (cipher == NULL || cipher_ctx == NULL) {
        fail("No se pudo preparar OpenSSL.");
    }

    unsigned char base_key[KEY_LEN];
    unsigned char iv[IV_LEN];
    unsigned char key[KEY_LEN];
    unsigned char cipher_text[MAX_MESSAGE_LEN];
    unsigned char plain_text[MAX_MESSAGE_LEN];

    /* Solo el rank 0 prepara el mensaje cifrado,
     * porque cada proceso generaria valores aleatorios distintos. */
    if (rank == 0) {
        if (RAND_bytes(base_key, KEY_LEN) != 1 || RAND_bytes(iv, IV_LEN) != 1) {
            fail("No se pudieron generar datos aleatorios.");
        }

        make_key(base_key, secret, bits, key);
        crypt_ctr(cipher_ctx, cipher, key, iv,
                  (const unsigned char *)message, cipher_text, message_len);
    }

    /* Todos reciben la parte conocida de la clave, el IV y el texto cifrado. */
    MPI_Bcast(base_key, KEY_LEN, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    MPI_Bcast(iv, IV_LEN, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);
    MPI_Bcast(cipher_text, message_len, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);

    uint64_t local_found_candidate = UINT64_MAX;
    uint64_t global_found_candidate = UINT64_MAX;
    uint64_t local_candidates_tested = 0;
    uint64_t total_candidates_tested = 0;
    uint64_t round_size = (uint64_t)CHECK_INTERVAL * (uint64_t)num_processes;

    /* Todos inician la medicion al mismo tiempo. */
    MPI_Barrier(MPI_COMM_WORLD);
    double start_time = MPI_Wtime();

    /*
     * Cada ronda cubre round_size candidatas consecutivas. Como el inicio
     * de cada ronda es multiplo de num_processes, el proceso r prueba
     * exactamente las candidatas c con c % num_processes == r. Todos
     * ejecutan el mismo numero de rondas, por lo que todos llegan a
     * cada MPI_Allreduce.
     */
    for (uint64_t round_start = 0; round_start < total_keys; round_start += round_size) {
        uint64_t round_end = round_start + round_size;

        if (round_end > total_keys) {
            round_end = total_keys;
        }

        for (uint64_t candidate = round_start + (uint64_t)rank;
             candidate < round_end;
             candidate += (uint64_t)num_processes) {
            make_key(base_key, candidate, bits, key);

            /* Solo se descifra el prefijo conocido: en CTR no es
             * necesario procesar el resto del mensaje para verificar. */
            crypt_ctr(cipher_ctx, cipher, key, iv, cipher_text, plain_text, prefix_len);
            local_candidates_tested++;

            if (memcmp(plain_text, message, prefix_len) == 0) {
                local_found_candidate = candidate;
                break;
            }
        }

        /* Si algun proceso encontro la clave, todos la conocen y se detienen. */
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
        printf("Ejecucion: paralela con %d procesos\n", num_processes);
        printf("Rango explorado: 2^%d candidatas\n", bits);

        if (global_found_candidate != UINT64_MAX) {
            /* Con la clave encontrada se descifra el mensaje completo. */
            make_key(base_key, global_found_candidate, bits, key);
            crypt_ctr(cipher_ctx, cipher, key, iv, cipher_text, plain_text, message_len);

            printf("Clave encontrada: candidata %" PRIu64
                   " (proceso %" PRIu64 ")\n",
                   global_found_candidate,
                   global_found_candidate % (uint64_t)num_processes);
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
