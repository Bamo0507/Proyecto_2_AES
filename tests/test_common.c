/*
 * Comprueba un ejemplo de cifrado conocido, la escritura de las claves y el
 * espacio para los mensajes.
 */
#include "aes_ctr.h"

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
    /*
     * Ejemplo de AES-128-CTR publicado por NIST: se comprueba que salga
     * exactamente el resultado esperado.
     * https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Standards-and-Guidelines/documents/examples/AES_CTR.pdf
     */
    const unsigned char reference_key[AES_KEY_LENGTH] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    const unsigned char reference_iv[AES_IV_LENGTH] = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };
    const unsigned char reference_plaintext[16] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
    };
    const unsigned char reference_ciphertext[16] = {
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
        0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce
    };
    AesCtrContext aes_context = {0};
    unsigned char ciphertext[AES_OUTPUT_CAPACITY];
    unsigned char plaintext[AES_OUTPUT_CAPACITY];
    CHECK(aes_ctr_context_open(&aes_context));
    CHECK(aes_ctr_transform(&aes_context, reference_key, reference_iv,
                             reference_plaintext, sizeof(reference_plaintext),
                             ciphertext));
    CHECK(memcmp(ciphertext, reference_ciphertext, sizeof(reference_ciphertext)) == 0);
    CHECK(aes_ctr_transform(&aes_context, reference_key, reference_iv,
                             reference_ciphertext, sizeof(reference_ciphertext),
                             plaintext));
    CHECK(memcmp(plaintext, reference_plaintext, sizeof(reference_plaintext)) == 0);

    unsigned char message[AES_MAX_MESSAGE_LENGTH];
    memset(message, 0x5a, sizeof(message));
    const size_t message_lengths[] = {1, 8, 16, AES_MAX_MESSAGE_LENGTH};
    for (size_t length_index = 0;
         length_index < sizeof(message_lengths) / sizeof(message_lengths[0]);
         length_index++) {
        size_t message_length = message_lengths[length_index];
        CHECK(aes_ctr_transform(&aes_context, reference_key, reference_iv,
                                 message, message_length, ciphertext));
        CHECK(aes_ctr_transform(&aes_context, reference_key, reference_iv,
                                 ciphertext, message_length, plaintext));
        CHECK(memcmp(plaintext, message, message_length) == 0);
    }
    CHECK(!aes_ctr_transform(&aes_context, reference_key, reference_iv, message, 0,
                              ciphertext));
    CHECK(!aes_ctr_transform(&aes_context, reference_key, reference_iv, message,
                              AES_MAX_MESSAGE_LENGTH + 1, ciphertext));

    unsigned char base_key[AES_KEY_LENGTH];
    unsigned char candidate_key[AES_KEY_LENGTH];
    memset(base_key, 0xaa, sizeof(base_key));
    aes_ctr_make_candidate_key(base_key, UINT64_C(0x123456789a), 40, candidate_key);
    const unsigned char expected_tail[] = {0x12, 0x34, 0x56, 0x78, 0x9a};
    CHECK(memcmp(candidate_key, base_key, 11) == 0);
    CHECK(memcmp(candidate_key + 11, expected_tail, sizeof(expected_tail)) == 0);
    aes_ctr_make_candidate_key(base_key, 1, 1, candidate_key);
    CHECK(memcmp(candidate_key, base_key, 15) == 0);
    CHECK(candidate_key[15] == 0xab);
    CHECK(base_key[15] == 0xaa);

    /*
     * Usa datos fijos para comprobar una clave correcta y descartar otra,
     * repitiendo siempre la misma prueba.
     */
    SearchOptions search_options = {8, 256, 123, "abcdefgh", 8, 8};
    AesSearchProblem search_problem = {0};
    memcpy(search_problem.base_key, base_key, AES_KEY_LENGTH);
    memcpy(search_problem.initialization_vector, reference_iv, AES_IV_LENGTH);
    aes_ctr_make_candidate_key(search_problem.base_key, search_options.secret_candidate,
                               search_options.unknown_key_bits, candidate_key);
    CHECK(aes_ctr_transform(&aes_context, candidate_key, search_problem.initialization_vector,
                             (const unsigned char *)search_options.message,
                             search_options.message_length, search_problem.ciphertext));
    int matches_prefix;
    CHECK(aes_ctr_test_candidate(&aes_context, &search_options, &search_problem, 123, &matches_prefix));
    CHECK(matches_prefix);
    CHECK(aes_ctr_test_candidate(&aes_context, &search_options, &search_problem, 122, &matches_prefix));
    CHECK(!matches_prefix);
    aes_ctr_context_close(&aes_context);
    aes_ctr_context_close(&aes_context);
    puts("OK: vector NIST, buffers, claves y verificacion CTR.");
    return EXIT_SUCCESS;
}
