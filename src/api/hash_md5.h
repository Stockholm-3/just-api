/**
 * hash_md5.h - Modul för MD5-hashning
 *
 * Baserad på Alexander Peslyaks MD5-implementering i det publika domänet
 * Förenklat gränssnitt för enkel integration i vilket projekt som helst
 *
 * Användning:
 *   char hash[33];
 *   hash_md5_string("Hello World", hash, sizeof(hash));
 *   printf("Hash: %s\n", hash);
 */

#ifndef HASH_MD5_H
#define HASH_MD5_H

#include <stddef.h>
#include <stdint.h>

/* MD5-hashlängd i hexadecimala tecken (32) + null-terminator */
#define HASH_MD5_STRING_LENGTH 33

/* MD5-hashlängd i byte (16) */
#define HASH_MD5_BINARY_LENGTH 16

/**
 * Beräkna MD5-hash av ett minnesblock och returnera som hex-sträng
 *
 * @param data Indata att hasha
 * @param data_size Storlek på indata i byte
 * @param output Buffert för hex-sträng (måste vara minst
 * HASH_MD5_STRING_LENGTH byte)
 * @param output_size Storlek på utdatabuffert
 * @return 0 vid framgång, -1 vid fel
 *
 * Exempel:
 *   char hash[HASH_MD5_STRING_LENGTH];
 *   hash_md5_string("Stockholm59.329318.0686", hash, sizeof(hash));
 *   // hash = "e7a8b9c0d1f2a3b4c5d6e7f8a9b0c1d2"
 */
int hash_md5_string(const void* data, size_t data_size, char* output,
                    size_t output_size);

/**
 * Beräkna MD5-hash och returnera som binär (16 byte)
 *
 * @param data Indata att hasha
 * @param data_size Storlek på indata i byte
 * @param output Buffert för binär hash (måste vara minst
 * HASH_MD5_BINARY_LENGTH byte)
 * @return 0 vid framgång, -1 vid fel
 */
int hash_md5_binary(const void* data, size_t data_size, unsigned char* output);

/**
 * Konvertera binär hash till hex-sträng
 *
 * @param binary Binär hash (16 byte)
 * @param output Buffert för hex-sträng (måste vara minst HASH_MD5_STRING_LENGTH
 * byte)
 * @param output_size Storlek på utdatabuffert
 * @return 0 vid framgång, -1 vid fel
 */
int hash_md5_binary_to_string(const unsigned char* binary, char* output,
                              size_t output_size);

#endif /* HASH_MD5_H */
