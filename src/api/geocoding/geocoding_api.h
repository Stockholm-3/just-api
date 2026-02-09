/**
 * geocoding_api.h - Geokodnings-API för att söka stadskoordinater
 *
 * Modul för att konvertera ett stadsnamn till geografiska koordinater.
 * Använder Open-Meteo Geocoding API med resultat-caching.
 */

#ifndef GEOCODING_API_H
#define GEOCODING_API_H

#include <stdbool.h>
#include <stddef.h>

#define GEOCODING_MAX_RESULTS 10

/* Maximum number of search results */
#define GEOCODING_MAX_RESULTS 10

/* Structure with city information */
typedef struct {
    float latitude;
    float longitude;
    char  name[128];       /* City name */
    char  country[64];     /* Country */
    char  country_code[8]; /* Country code (UA, US, etc.) */
    char  admin1[64];      /* Region / province / state */
    char  admin2[64];      /* District */
    int   population;      /* Population */
    char  timezone[64];    /* Time zone */
    int   id;              /* Place ID */
} GeocodingResult;

/* Structure with search results */
typedef struct {
    GeocodingResult* results; /* Array of results */
    int              count;   /* Number of found results */
} GeocodingResponse;

/* Configuration for the geocoding module */
typedef struct {
    const char* cache_dir;   /* Directory for cache files */
    int         cache_ttl;   /* Cache TTL in seconds (default: 7 days) */
    bool        use_cache;   /* Use cache */
    int         max_results; /* Maximum number of results */
    const char* language;    /* Result language (uk, en, ru, etc.) */
} GeocodingConfig;

/**
 * Initierar geokodnings-API:t
 *
 * @param config Konfiguration (NULL för standardinställningar)
 * @return 0 vid framgång, -1 vid fel
 */
int geocoding_api_init(GeocodingConfig* config);

/**
 * Sök efter en stad med namn
 *
 * @param city_name Stadsnamn att söka efter (obligatoriskt)
 * @param country Landskod för att filtrera resultat (valfritt, kan vara NULL)
 * @param response Pekare till sökresultatet (anropande kod måste frigöra)
 * @return 0 vid framgång, < 0 vid fel
 *
 * Exempel:
 *   geocoding_api_search("Kyiv", "UA", &response);
 *   geocoding_api_search("Stockholm", NULL, &response);
 *   geocoding_api_search("London", "GB", &response);
 */
int geocoding_api_search(const char* city_name, const char* country,
                         GeocodingResponse** response);

/*
 * Sök utan att skriva till cache. Användbart för endpoints som inte ska
 * skapa eller uppdatera den delade stadscachen (t.ex. autocomplete /cities).
 */
int geocoding_api_search_no_cache(const char* city_name, const char* country,
                                  GeocodingResponse** response);

/* Försök först ladda resultat från cache (read-only). Om cache saknas eller
 * har gått ut, hämta från API men SPARA INTE resultaten i cachen. Returnerar
 * 0 vid framgång och fyller `response` (anropande kod måste frigöra). */
int geocoding_api_search_readonly_cache(const char*         city_name,
                                        const char*         country,
                                        GeocodingResponse** response);

/**
 * Smart sökning med 3-nivåers fallback-strategi
 *
 * Söker i följande ordning:
 * 1. Databas med populära städer (i minnet, snabbast)
 * 2. Filcache (snabbt)
 * 3. Open-Meteo API (långsammare, använder kvota)
 *
 * @param query Söksträng (minst 2 tecken)
 * @param response Pekare till sökresultat (anropande kod måste frigöra)
 * @return 0 vid framgång, < 0 vid fel
 *
 * Funktionen minskar API-anrop för autocomplete genom att först kontrollera
 * lokala databaser.
 */
int geocoding_api_search_smart(const char* query, GeocodingResponse** response);

/**
 * Sök efter en stad med extra regionsfilter
 *
 * @param city_name Stadsnamn
 * @param region Region/provins (valfritt)
 * @param country Landskod (valfritt)
 * @param response Pekare till sökresultatet
 * @return 0 vid framgång, < 0 vid fel
 *
 * Exempel:
 *   geocoding_api_search_detailed("Lviv", "Lviv Oblast", "UA", &response);
 */
int geocoding_api_search_detailed(const char* city_name, const char* region,
                                  const char*         country,
                                  GeocodingResponse** response);

/**
 * Hämta bästa resultatet (det med störst befolkning)
 *
 * @param response Sökresultat
 * @return Pekare till bästa resultat eller NULL
 */
GeocodingResult* geocoding_api_get_best_result(GeocodingResponse* response,
                                               const char*        country);

/**
 * Frigör minnet för ett sökresultat
 *
 * @param response Resultat att frigöra
 */
void geocoding_api_free_response(GeocodingResponse* response);

/**
 * Rensa cachen
 *
 * @return 0 vid framgång
 */
int geocoding_api_clear_cache(void);

/**
 * Avsluta och rensa upp geokodningsmodulen
 */
void geocoding_api_cleanup(void);

/**
 * Formatera ett resultat till en läsbar sträng
 *
 * @param result Geokodningsresultat
 * @param buffer Utdatabuffer
 * @param buffer_size Buffertstorlek
 * @return 0 vid framgång
 *
 * Exempelutdata: "Kyiv, Kyiv Oblast, Ukraine (50.4501, 30.5234)"
 */
int geocoding_api_format_result(GeocodingResult* result, char* buffer,
                                size_t buffer_size);

#endif /* GEOCODING_API_H */