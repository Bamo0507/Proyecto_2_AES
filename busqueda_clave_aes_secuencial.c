/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3169 - Computacion Paralela y Distribuida
 * Miembros:    Adriana Palacios
 *              Bryan Martinez
 *              Daniela Ramirez
 * Ejercicio:   Busqueda secuencial de una clave AES mediante fuerza bruta
 * Descripcion: un unico proceso prueba claves candidatas en orden
 *              y compara el mensaje descifrado con el texto original.
 *
 *              Para fines educativos, la busqueda se limita a
 *              1,048,576 candidatas del espacio de claves de AES.
 *              El cifrado utiliza la interfaz EVP de OpenSSL.
 *              Se mide el tiempo transcurrido durante la busqueda.
 *----------------------------------------------------------------------*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <openssl/evp.h>
#include <openssl/err.h>

#define TOTAL_KEYS (UINT64_C(1) << 20)
#define SECRET_KEY UINT64_C(12345)
#define MESSAGE_LEN 16

/* Tamano de una clave AES-128, en bytes. */
#define KEY_LEN 16

/* Bytes finales de la clave donde se escribe la candidata (64 bits). */
#define TAIL_BYTES 8

/* Texto original de 16 bytes; el cero final no se cifra. */
static const unsigned char message[] = "Puedes lograrlo!";

_Static_assert(
    sizeof(message) - 1 == MESSAGE_LEN,
    "El mensaje debe tener exactamente 16 bytes."
);

/* Muestra el error y termina el programa. */
static void fail(const char *description)
{
    fprintf(stderr, "%s\n", description);
    ERR_print_errors_fp(stderr);
    exit(EXIT_FAILURE);
}

/*
 * Construye una clave AES no aleatoria a partir de la candidata:
 * los primeros bytes quedan en cero y la candidata se escribe en
 * los ultimos TAIL_BYTES bytes, en orden big-endian.
 */
static void make_key(uint64_t candidate, unsigned char key[KEY_LEN])
{
    memset(key, 0, KEY_LEN);

    for (int byte_index = 0; byte_index < TAIL_BYTES; byte_index++) {
        key[KEY_LEN - 1 - byte_index] = (unsigned char)(
            (candidate >> (8 * byte_index)) & UINT64_C(0xFF)
        );
    }
}

/*
 * Cifra o descifra un bloque mediante AES-128-ECB.
 * encrypt = 1: cifrar; encrypt = 0: descifrar.
 * ECB se utiliza para un unico bloque educativo.
 */
static void crypt_block(
    EVP_CIPHER_CTX *cipher_ctx,
    uint64_t candidate,
    const unsigned char *input,
    unsigned char *output,
    int encrypt)
{
    unsigned char key[KEY_LEN];
    int written = 0;
    int final_written = 0;

    make_key(candidate, key);

    if (EVP_CipherInit_ex(
            cipher_ctx, EVP_aes_128_ecb(), NULL,
            key, NULL, encrypt) != 1) {
        fail("Error al inicializar AES.");
    }

    /* El mensaje ocupa exactamente un bloque, sin relleno. */
    if (EVP_CIPHER_CTX_set_padding(cipher_ctx, 0) != 1) {
        fail("Error al configurar el relleno.");
    }

    if (EVP_CipherUpdate(
            cipher_ctx, output, &written,
            input, MESSAGE_LEN) != 1) {
        fail("Error al procesar el bloque.");
    }

    if (EVP_CipherFinal_ex(
            cipher_ctx, output + written, &final_written) != 1) {
        fail("Error al finalizar la operacion AES.");
    }

    if (written + final_written != MESSAGE_LEN) {
        fail("Longitud inesperada del resultado.");
    }
}

/* Obtiene el tiempo de un reloj monotono, en segundos. */
static double get_time(void)
{
    struct timespec current;

    if (clock_gettime(CLOCK_MONOTONIC, &current) != 0) {
        perror("Error al consultar el reloj");
        exit(EXIT_FAILURE);
    }

    return (double)current.tv_sec +
           (double)current.tv_nsec / 1000000000.0;
}

/*
 * Cifra el mensaje con SECRET_KEY, prueba las candidatas desde cero
 * hasta encontrar la que lo descifra y reporta la clave y el tiempo.
 */
int main(void)
{
    EVP_CIPHER_CTX *cipher_ctx = EVP_CIPHER_CTX_new();

    if (cipher_ctx == NULL) {
        fail("No se pudo crear el contexto de OpenSSL.");
    }

    unsigned char cipher_text[
        MESSAGE_LEN + EVP_MAX_BLOCK_LENGTH
    ] = {0};

    unsigned char plain_text[
        MESSAGE_LEN + EVP_MAX_BLOCK_LENGTH
    ] = {0};

    /*
     * Prepara el mensaje cifrado.
     * SECRET_KEY solo se utiliza para preparar el ejercicio.
     */
    crypt_block(cipher_ctx, SECRET_KEY, message, cipher_text, 1);

    uint64_t found_key = UINT64_MAX;

    /* La medicion comienza despues de preparar el mensaje. */
    double start_time = get_time();

    /* Prueba las claves consecutivamente desde cero. */
    for (uint64_t candidate = 0; candidate < TOTAL_KEYS; candidate++) {
        crypt_block(cipher_ctx, candidate, cipher_text, plain_text, 0);

        /* Verifica todos los bytes del mensaje conocido. */
        if (memcmp(plain_text, message, MESSAGE_LEN) == 0) {
            found_key = candidate;
            break;
        }
    }

    double elapsed_seconds = get_time() - start_time;

    if (found_key != UINT64_MAX) {
        printf("Clave encontrada: %" PRIu64 "\n", found_key);
        printf("Mensaje: ");
        fwrite(plain_text, 1, MESSAGE_LEN, stdout);
        putchar('\n');
    } else {
        printf("No se encontro la clave.\n");
    }

    printf("Ejecucion: secuencial\n");
    printf("Tiempo: %.6f segundos\n", elapsed_seconds);

    EVP_CIPHER_CTX_free(cipher_ctx);

    return EXIT_SUCCESS;
}
