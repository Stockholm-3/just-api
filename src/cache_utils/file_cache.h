/**
 * file_cache.h - Enhetlig filbaserad cache-modul
 *
 * Tillhandahåller ett generiskt cache-API med TTL-baserad utgång,
 * MD5-nyckelgenerering och JSON-stöd via jansson.
 */

#ifndef FILE_CACHE_H
#define FILE_CACHE_H

#include <stdbool.h>
#include <stddef.h>

#define FILE_CACHE_MAX_PATH_LENGTH 512
#define FILE_CACHE_KEY_LENGTH 33 /* MD5 hex-sträng + null-terminator */

/* Resultatkoder för cache-operationer */
typedef enum {
    FILE_CACHE_OK              = 0,
    FILE_CACHE_ERROR_PARAM     = -1, /* Ogiltigt parameter */
    FILE_CACHE_ERROR_NOT_FOUND = -2, /* Cache-post hittades inte */
    FILE_CACHE_ERROR_EXPIRED   = -3, /* Cache-post har gått ut */
    FILE_CACHE_ERROR_IO        = -4, /* Fil-I/O-fel */
    FILE_CACHE_ERROR_MEMORY    = -5, /* Minnesallokering misslyckades */
    FILE_CACHE_ERROR_HASH      = -6, /* Hash-beräkning misslyckades */
    FILE_CACHE_ERROR_PARSE     = -7  /* JSON-parsningsfel */
} FileCacheResult;

/* Konfiguration för en cache-instans */
typedef struct {
    const char* cache_dir;   /* Katalog för cache-filer */
    int         ttl_seconds; /* Livstid i sekunder */
    bool        enabled;     /* Huruvida cachen är aktiverad */
} FileCacheConfig;

/* Ogenomskinligt handtag för cache-instans */
typedef struct FileCacheInstance FileCacheInstance;

/* ============= Livscykel ============= */

/**
 * Skapa och initiera en ny cache-instans.
 * Skapar cache-katalogen om den inte finns.
 *
 * @param config  Konfiguration för denna cache-instans
 * @return        Pekare till cache-instans, eller NULL vid fel
 */
FileCacheInstance* file_cache_create(const FileCacheConfig* config);

/**
 * Förstör en cache-instans och frigör resurser.
 *
 * @param cache  Cache-instans som ska förstöras
 */
void file_cache_destroy(FileCacheInstance* cache);

/* ============= Kärnoperationer ============= */

/**
 * Generera en cache-nyckel (MD5-hash) från indatasträng.
 *
 * @param cache     Cache-instans
 * @param input     Indatasträng att hasha
 * @param out_key   Utdatabuffert för hex-hash (>= FILE_CACHE_KEY_LENGTH)
 * @param key_size  Storlek på out_key-buffert
 * @return          FILE_CACHE_OK vid framgång, felkod annars
 */
FileCacheResult file_cache_generate_key(FileCacheInstance* cache,
                                        const char* input, char* out_key,
                                        size_t key_size);

/**
 * Kontrollera om en cache-post finns och inte har gått ut.
 *
 * @param cache      Cache-instans
 * @param cache_key  Cache-nyckeln (MD5-hash från file_cache_generate_key)
 * @return           true om giltig cache-post finns, false annars
 */
bool file_cache_is_valid(FileCacheInstance* cache, const char* cache_key);

/**
 * Ladda rådata från cache-fil.
 * Kontrollerar TTL innan laddning. Returnerar FILE_CACHE_ERROR_EXPIRED om posten är inaktuell.
 *
 * @param cache      Cache-instans
 * @param cache_key  Cache-nyckeln
 * @param out_data   Utdatapekare för laddad data (anroparen måste frigöra)
 * @param out_size   Utdatastorlek för laddad data (valfri, kan vara NULL)
 * @return           FILE_CACHE_OK vid framgång, felkod annars
 */
FileCacheResult file_cache_load(FileCacheInstance* cache, const char* cache_key,
                                char** out_data, size_t* out_size);

/**
 * Spara rådata till cache-fil.
 *
 * @param cache      Cache-instans
 * @param cache_key  Cache-nyckeln
 * @param data       Data att spara
 * @param data_size  Datastorlek i byte (0 = använd strlen)
 * @return           FILE_CACHE_OK vid framgång, felkod annars
 */
FileCacheResult file_cache_save(FileCacheInstance* cache, const char* cache_key,
                                const char* data, size_t data_size);

/* ============= JSON-hjälpfunktioner ============= */

/**
 * Ladda och parsa JSON från cache (bekvämlighetsomslagarfunktion).
 *
 * @param cache      Cache-instans
 * @param cache_key  Cache-nyckeln
 * @param out_json   Utdatapekare för parsad JSON (jansson json_t*)
 *                   Anroparen måste anropa json_decref() när klar.
 * @return           FILE_CACHE_OK vid framgång, felkod annars
 */
FileCacheResult file_cache_load_json(FileCacheInstance* cache,
                                     const char* cache_key, void** out_json);

/**
 * Serialisera och spara JSON till cache (bekvämlighetsomslagarfunktion).
 *
 * @param cache      Cache-instans
 * @param cache_key  Cache-nyckeln
 * @param json       JSON-objekt att spara (jansson json_t*)
 * @return           FILE_CACHE_OK vid framgång, felkod annars
 */
FileCacheResult file_cache_save_json(FileCacheInstance* cache,
                                     const char* cache_key, void* json);

/* ============= Cache-hantering ============= */

/**
 * Ogiltigförklara (ta bort) en specifik cache-post.
 *
 * @param cache      Cache-instans
 * @param cache_key  Cache-nyckeln
 * @return           FILE_CACHE_OK vid framgång, felkod annars
 */
FileCacheResult file_cache_invalidate(FileCacheInstance* cache,
                                      const char*        cache_key);

/**
 * Rensa alla cache-poster för denna cache-instans.
 *
 * @param cache  Cache-instans
 * @return       FILE_CACHE_OK vid framgång, felkod annars
 */
FileCacheResult file_cache_clear(FileCacheInstance* cache);

/* ============= Hjälpfunktioner ============= */

/**
 * Normalisera en sträng för användning som cache-nyckelindata.
 * Konverterar till gemener, komprimerar blanksteg till understreck,
 * och trimmar ledande/avslutande understreck.
 *
 * @param input        Indatasträng
 * @param output       Utdatabuffert
 * @param output_size  Storlek på utdatabuffert
 * @return             FILE_CACHE_OK vid framgång, felkod annars
 */
FileCacheResult file_cache_normalize_string(const char* input, char* output,
                                            size_t output_size);

/**
 * Hämta den fullständiga filsökvägen för en cache-post.
 *
 * @param cache      Cache-instans
 * @param cache_key  Cache-nyckeln
 * @param out_path   Utdatabuffert för filsökväg
 * @param path_size  Storlek på out_path-buffert
 * @return           FILE_CACHE_OK vid framgång, felkod annars
 */
FileCacheResult file_cache_get_filepath(FileCacheInstance* cache,
                                        const char* cache_key, char* out_path,
                                        size_t path_size);

#endif /* FILE_CACHE_H */
