/*----------------------------------------------------------------------
 * UNIVERSIDAD DEL VALLE DE GUATEMALA
 * Curso:       CC3169 - Computacion Paralela y Distribuida
 * Miembros:    Adriana Palacios
 *              Bryan Martinez
 *              Daniela Ramirez
 * Ejercicio:   Busqueda secuencial de una clave AES mediante fuerza bruta
 * Descripcion: un trabajador prueba posibles claves una por una
 *              y compara el mensaje recuperado con el original.
 *
 *              Para fines educativos, la busqueda se limita a
 *              1,048,576 candidatas del espacio de claves de AES.
 *              OpenSSL realiza el cifrado del mensaje.
 *              Se mide el tiempo transcurrido durante la busqueda.
 *              Se conserva como el ejemplo original para estudiar el proceso.
 *              Uso: ./build/bin/aes_search_ecb
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

#define TOTAL_CANDIDATES (UINT64_C(1) << 20)
#define SECRET_CANDIDATE UINT64_C(12345)
#define MESSAGE_LENGTH 16

/* Espacio que ocupa una clave AES-128, en bytes. */
#define AES_KEY_LENGTH 16

/* Parte final de la clave donde se escribe el número que se va a probar. */
#define CANDIDATE_KEY_BYTES 8

/* El texto ocupa 16 bytes; su marca final no se cifra. */
static const unsigned char original_message[] = "Puedes lograrlo!";

_Static_assert(
    sizeof(original_message) - 1 == MESSAGE_LENGTH,
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
 * Construye la clave a partir del número candidato. Completa el inicio con
 * ceros y escribe el número en los últimos ocho bytes, con la parte de mayor
 * valor primero.
 */
static void make_key(uint64_t candidate, unsigned char candidate_key[AES_KEY_LENGTH])
{
    memset(candidate_key, 0, AES_KEY_LENGTH);

    for (int byte_index = 0; byte_index < CANDIDATE_KEY_BYTES; byte_index++) {
        candidate_key[AES_KEY_LENGTH - 1 - byte_index] = (unsigned char)(
            (candidate >> (8 * byte_index)) & UINT64_C(0xFF)
        );
    }
}

/*
 * Cifra o descifra los 16 bytes del mensaje con AES-128-ECB. encrypt_message
 * vale 1 para cifrar y 0 para descifrar. Este ejemplo utiliza un solo grupo de
 * 16 bytes.
 */
static void crypt_block(
    EVP_CIPHER_CTX *cipher_context,
    uint64_t candidate,
    const unsigned char *input_message,
    unsigned char *output_message,
    int encrypt_message)
{
    unsigned char candidate_key[AES_KEY_LENGTH];
    int bytes_written = 0;
    int final_bytes_written = 0;

    make_key(candidate, candidate_key);

    if (EVP_CipherInit_ex(
            cipher_context, EVP_aes_128_ecb(), NULL,
            candidate_key, NULL, encrypt_message) != 1) {
        fail("Error al inicializar AES.");
    }

    /* El mensaje ya ocupa los 16 bytes completos y no necesita agregar otros. */
    if (EVP_CIPHER_CTX_set_padding(cipher_context, 0) != 1) {
        fail("Error al configurar el relleno.");
    }

    if (EVP_CipherUpdate(
            cipher_context, output_message, &bytes_written,
            input_message, MESSAGE_LENGTH) != 1) {
        fail("Error al procesar el bloque.");
    }

    if (EVP_CipherFinal_ex(
            cipher_context, output_message + bytes_written, &final_bytes_written) != 1) {
        fail("Error al finalizar la operacion AES.");
    }

    if (bytes_written + final_bytes_written != MESSAGE_LENGTH) {
        fail("Longitud inesperada del resultado.");
    }
}

/* Consulta un reloj que mide duración sin verse afectado por cambios de la hora. */
static double get_time(void)
{
    struct timespec current_time;

    if (clock_gettime(CLOCK_MONOTONIC, &current_time) != 0) {
        perror("Error al consultar el reloj");
        exit(EXIT_FAILURE);
    }

    return (double)current_time.tv_sec +
           (double)current_time.tv_nsec / 1000000000.0;
}

/*
 * Prepara el mensaje con SECRET_CANDIDATE. Después prueba claves desde cero y
 * muestra la que recupera el mensaje, junto con el tiempo de búsqueda.
 */
int main(void)
{
    EVP_CIPHER_CTX *cipher_context = EVP_CIPHER_CTX_new();

    if (cipher_context == NULL) {
        fail("No se pudo crear el contexto de OpenSSL.");
    }

    unsigned char ciphertext[
        MESSAGE_LENGTH + EVP_MAX_BLOCK_LENGTH
    ] = {0};

    unsigned char recovered_message[
        MESSAGE_LENGTH + EVP_MAX_BLOCK_LENGTH
    ] = {0};

    /*
     * Usa SECRET_CANDIDATE para crear el mensaje cifrado antes de comenzar la
     * búsqueda.
     */
    crypt_block(cipher_context, SECRET_CANDIDATE, original_message, ciphertext, 1);

    uint64_t found_candidate = UINT64_MAX;

    /* Empieza a medir después de preparar el mensaje. */
    double start_seconds = get_time();

    /* Prueba las claves en orden desde cero. */
    for (uint64_t candidate = 0; candidate < TOTAL_CANDIDATES; candidate++) {
        crypt_block(cipher_context, candidate, ciphertext, recovered_message, 0);

        /* Compara todo el mensaje recuperado con el original. */
        if (memcmp(recovered_message, original_message, MESSAGE_LENGTH) == 0) {
            found_candidate = candidate;
            break;
        }
    }

    double elapsed_seconds = get_time() - start_seconds;

    if (found_candidate != UINT64_MAX) {
        printf("Clave encontrada: %" PRIu64 "\n", found_candidate);
        printf("Mensaje: ");
        fwrite(recovered_message, 1, MESSAGE_LENGTH, stdout);
        putchar('\n');
    } else {
        printf("No se encontro la clave.\n");
    }

    printf("Ejecucion: secuencial\n");
    printf("Tiempo: %.6f segundos\n", elapsed_seconds);

    EVP_CIPHER_CTX_free(cipher_context);

    return EXIT_SUCCESS;
}
