/**
 * hash_md5.h - Modular MD5 hashing utility
 *
 * Based on Alexander Peslyak's public domain MD5 implementation
 * Simplified interface for easy integration into any project
 *
 * Usage:
 *   char hash[33];
 *   hash_md5_string("Hello World", hash, sizeof(hash));
 *   printf("Hash: %s\n", hash);
 */

#ifndef HASH_MD5_H
#define HASH_MD5_H

#include <stddef.h>
#include <stdint.h>

/* MD5 hash length in hexadecimal characters (32) + null terminator */
#define HASH_MD5_STRING_LENGTH 33

/* MD5 hash length in bytes (16) */
#define HASH_MD5_BINARY_LENGTH 16

/**
 * Calculate MD5 hash of a memory block and return as hex string
 *
 * @param data Input data to hash
 * @param data_size Size of input data in bytes
 * @param output Buffer to store hex string (must be at least
 * HASH_MD5_STRING_LENGTH bytes)
 * @param output_size Size of output buffer
 * @return 0 on success, -1 on error
 *
 * Example:
 *   char hash[HASH_MD5_STRING_LENGTH];
 *   hash_md5_string("Stockholm59.329318.0686", hash, sizeof(hash));
 *   // hash = "e7a8b9c0d1f2a3b4c5d6e7f8a9b0c1d2"
 */
int hash_md5_string(const void* data, size_t data_size, char* output,
                    size_t output_size);

/**
 * Calculate MD5 hash and return as binary (16 bytes)
 *
 * @param data Input data to hash
 * @param data_size Size of input data in bytes
 * @param output Buffer to store binary hash (must be at least
 * HASH_MD5_BINARY_LENGTH bytes)
 * @return 0 on success, -1 on error
 */
int hash_md5_binary(const void* data, size_t data_size, unsigned char* output);

/**
 * Convert binary hash to hex string
 *
 * @param binary Binary hash (16 bytes)
 * @param output Buffer for hex string (must be at least HASH_MD5_STRING_LENGTH
 * bytes)
 * @param output_size Size of output buffer
 * @return 0 on success, -1 on error
 */
int hash_md5_binary_to_string(const unsigned char* binary, char* output,
                              size_t output_size);

#endif /* HASH_MD5_H */
