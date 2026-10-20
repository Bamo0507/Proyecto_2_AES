/*
 * Usa OpenSSL para cifrar, descifrar y construir posibles claves. Las búsquedas
 * comparten estas operaciones y eligen por separado el orden de las pruebas.
 */
#include "aes_ctr.h"

#include <string.h>
#include <limits.h>

int aes_ctr_context_open(AesCtrContext *aes_context)
{
    aes_context->cipher = EVP_CIPHER_fetch(NULL, "AES-128-CTR", NULL);
    aes_context->cipher_context = EVP_CIPHER_CTX_new();
    if (aes_context->cipher == NULL || aes_context->cipher_context == NULL) {
        /* Si solo se preparó una parte, también hay que liberarla. */
        aes_ctr_context_close(aes_context);
        return 0;
    }
    return 1;
}

void aes_ctr_context_close(AesCtrContext *aes_context)
{
    EVP_CIPHER_CTX_free(aes_context->cipher_context);
    EVP_CIPHER_free(aes_context->cipher);
    /* Dejar los campos vacíos permite cerrar de nuevo sin liberar lo mismo dos veces. */
    aes_context->cipher_context = NULL;
    aes_context->cipher = NULL;
}

void aes_ctr_make_candidate_key(const unsigned char base_key[AES_KEY_LENGTH],
                                uint64_t candidate, int unknown_key_bits,
                                unsigned char candidate_key[AES_KEY_LENGTH])
{
    uint64_t candidate_mask = (UINT64_C(1) << unknown_key_bits) - 1;
    uint64_t key_tail = 0;
    const int tail_start = AES_KEY_LENGTH - AES_KEY_TAIL_BYTES;
    memcpy(candidate_key, base_key, AES_KEY_LENGTH);

    /* Lee la parte final como un número, empezando por los bytes de mayor valor. */
    for (int byte_index = 0; byte_index < AES_KEY_TAIL_BYTES; byte_index++) {
        key_tail = (key_tail << 8) | candidate_key[tail_start + byte_index];
    }
    /* Cambia solo la parte que se busca y conserva los demás bits de la clave. */
    key_tail = (key_tail & ~candidate_mask) | (candidate & candidate_mask);
    /* Escribe el número desde el final para mantener el mismo orden en distintas máquinas. */
    for (int byte_index = AES_KEY_TAIL_BYTES - 1; byte_index >= 0; byte_index--) {
        candidate_key[tail_start + byte_index] = (unsigned char)(key_tail & 0xFF);
        key_tail >>= 8;
    }
}

int aes_ctr_transform_buffer(AesCtrContext *aes_context,
                             const unsigned char key[AES_KEY_LENGTH],
                             const unsigned char initialization_vector[AES_IV_LENGTH],
                             const unsigned char *input, size_t input_length,
                             unsigned char *output, size_t output_capacity)
{
    if (input_length == 0 ||
        input_length > (size_t)(INT_MAX - EVP_MAX_BLOCK_LENGTH) ||
        output_capacity < input_length + EVP_MAX_BLOCK_LENGTH) {
        return 0;
    }
    int bytes_written = 0;
    int final_bytes_written = 0;
    /*
     * Con CTR se siguen los mismos pasos para cifrar y descifrar. Si uno falla,
     * no se continúa con los siguientes.
     */
    if (EVP_CipherInit_ex(aes_context->cipher_context, aes_context->cipher, NULL,
                         key, initialization_vector, 1) != 1 ||
        EVP_CipherUpdate(aes_context->cipher_context, output, &bytes_written,
                         input, (int)input_length) != 1 ||
        EVP_CipherFinal_ex(aes_context->cipher_context, output + bytes_written,
                           &final_bytes_written) != 1) {
        return 0;
    }
    /* El mensaje debe ocupar lo mismo antes y después del cifrado. */
    return bytes_written + final_bytes_written == (int)input_length;
}
