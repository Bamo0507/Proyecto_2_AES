/**
 * @file aes_ctr.c
 * @brief Implementacion de la preparacion y verificacion criptografica CTR.
 *
 * Separa el trabajo por candidata del recorrido secuencial o distribuido. EVP
 * administra las operaciones AES y RAND_bytes genera los datos del ejercicio.
 * Los contratos publicos, capacidades y reglas de propiedad estan en aes_ctr.h.
 * Este archivo no realiza comunicacion MPI ni termina procesos ante un error.
 */
#include "aes_ctr.h"

#include <string.h>
#include <openssl/rand.h>

int aes_ctr_context_open(AesCtrContext *context)
{
    context->cipher = EVP_CIPHER_fetch(NULL, "AES-128-CTR", NULL);
    context->cipher_context = EVP_CIPHER_CTX_new();
    if (context->cipher == NULL || context->cipher_context == NULL) {
        /* Cubre tambien el caso en que solo una de las dos reservas tuvo exito. */
        aes_ctr_context_close(context);
        return 0;
    }
    return 1;
}

void aes_ctr_context_close(AesCtrContext *context)
{
    EVP_CIPHER_CTX_free(context->cipher_context);
    EVP_CIPHER_free(context->cipher);
    /* Dejar la instancia cerrada permite repetir el cleanup sin doble liberacion. */
    context->cipher_context = NULL;
    context->cipher = NULL;
}

void aes_ctr_make_candidate_key(const unsigned char base_key[AES_KEY_LENGTH],
                                uint64_t candidate, int unknown_key_bits,
                                unsigned char candidate_key[AES_KEY_LENGTH])
{
    uint64_t candidate_mask = (UINT64_C(1) << unknown_key_bits) - 1;
    uint64_t key_tail = 0;
    const int tail_start = AES_KEY_LENGTH - AES_KEY_TAIL_BYTES;
    memcpy(candidate_key, base_key, AES_KEY_LENGTH);

    /* La cola se interpreta como un entero big-endian de 64 bits. */
    for (int byte_index = 0; byte_index < AES_KEY_TAIL_BYTES; byte_index++) {
        key_tail = (key_tail << 8) | candidate_key[tail_start + byte_index];
    }
    /* Borra solo los bits desconocidos; la mascara conserva el resto de la base. */
    key_tail = (key_tail & ~candidate_mask) | (candidate & candidate_mask);
    /* Escribir desde el final mantiene el mismo orden en maquinas little-endian. */
    for (int byte_index = AES_KEY_TAIL_BYTES - 1; byte_index >= 0; byte_index--) {
        candidate_key[tail_start + byte_index] = (unsigned char)(key_tail & 0xFF);
        key_tail >>= 8;
    }
}

int aes_ctr_transform(AesCtrContext *context,
                      const unsigned char key[AES_KEY_LENGTH],
                      const unsigned char initialization_vector[AES_IV_LENGTH],
                      const unsigned char *input, size_t input_length,
                      unsigned char output[AES_OUTPUT_CAPACITY])
{
    /* El limite del ejercicio tambien hace segura la conversion a int de EVP. */
    if (input_length == 0 || input_length > AES_MAX_MESSAGE_LENGTH) {
        return 0;
    }
    int bytes_written = 0;
    int final_bytes_written = 0;
    /* CTR usa la direccion de cifrado tambien para descifrar. La evaluacion
     * en cortocircuito evita continuar una operacion EVP que ya fallo. */
    if (EVP_CipherInit_ex(context->cipher_context, context->cipher, NULL,
                         key, initialization_vector, 1) != 1 ||
        EVP_CipherUpdate(context->cipher_context, output, &bytes_written,
                         input, (int)input_length) != 1 ||
        EVP_CipherFinal_ex(context->cipher_context, output + bytes_written,
                           &final_bytes_written) != 1) {
        return 0;
    }
    /* Sin relleno, el total producido debe coincidir con la longitud de entrada. */
    return bytes_written + final_bytes_written == (int)input_length;
}

int aes_ctr_prepare_problem(AesCtrContext *context, const SearchOptions *options,
                            AesSearchProblem *problem)
{
    unsigned char secret_key[AES_KEY_LENGTH];
    /* La base y el contador se generan juntos una sola vez por problema. */
    if (RAND_bytes(problem->base_key, AES_KEY_LENGTH) != 1 ||
        RAND_bytes(problem->initialization_vector, AES_IV_LENGTH) != 1) {
        return 0;
    }
    /* La candidata secreta solo interviene al preparar el ejercicio. */
    aes_ctr_make_candidate_key(problem->base_key, options->secret_candidate,
                               options->unknown_key_bits, secret_key);
    return aes_ctr_transform(context, secret_key, problem->initialization_vector,
                              (const unsigned char *)options->message,
                              options->message_length, problem->ciphertext);
}

int aes_ctr_test_candidate(AesCtrContext *context, const SearchOptions *options,
                           const AesSearchProblem *problem, uint64_t candidate,
                           int *matches_prefix)
{
    unsigned char candidate_key[AES_KEY_LENGTH];
    unsigned char plaintext[AES_OUTPUT_CAPACITY];
    aes_ctr_make_candidate_key(problem->base_key, candidate,
                               options->unknown_key_bits, candidate_key);
    /* Reiniciar CTR con el IV original permite verificar solo el prefijo. */
    if (!aes_ctr_transform(context, candidate_key, problem->initialization_vector,
                            problem->ciphertext, options->prefix_length,
                            plaintext)) {
        return 0;
    }
    /* El mensaje es binario durante la comparacion; no se usa un terminador nulo. */
    *matches_prefix = memcmp(plaintext, options->message,
                             options->prefix_length) == 0;
    return 1;
}
