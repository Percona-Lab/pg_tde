/*-------------------------------------------------------------------------
 *
 * end_aes.h
 *	  AES Encryption / Decryption routines using OpenSSL
 *
 * src/include/encryption/enc_aes.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef ENC_AES_H
#define ENC_AES_H

#include <stdint.h>

#define AES_BLOCK_SIZE 		        16
#define NUM_AES_BLOCKS_IN_BATCH     100
#define DATA_BYTES_PER_AES_BATCH    (NUM_AES_BLOCKS_IN_BATCH * AES_BLOCK_SIZE)
#define MAX_AES_ENC_BATCH_KEY_SIZE  ((NUM_AES_BLOCKS_IN_BATCH + 2) * AES_BLOCK_SIZE)


void AesInit(void);
extern void Aes128EncryptedZeroBlocks(const unsigned char* key, uint64_t blockNumber1, uint64_t blockNumber2, unsigned char* out);
extern void Aes128EncryptedZeroBlocks2(void* ctxPtr, const unsigned char* key, uint64_t blockNumber1, uint64_t blockNumber2, unsigned char* out);

/* Only used for testing */
extern void AesEncrypt(const unsigned char* key, const unsigned char* iv, const unsigned char* in, int in_len, unsigned char* out, int* out_len);
extern void AesDecrypt(const unsigned char* key, const unsigned char* iv, const unsigned char* in, int in_len, unsigned char* out, int* out_len);

#endif /*ENC_AES_H*/