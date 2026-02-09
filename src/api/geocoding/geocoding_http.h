/**
 * geocoding_http.h - HTTP- och URL-hjälpare för geokodnings-API
 */
#ifndef GEOCODING_HTTP_H
#define GEOCODING_HTTP_H

#include <geocoding_api.h>

/* Bygg API-URL för en stad (anropande kod måste free:a strängen) */
char* geocoding_build_api_url(const char* city_name, const char* country,
                              int max_results, const char* language);

/* Synkron hämtning av angiven URL. Returnerar 0 vid framgång och fyller
 * `response_data` (anropande kod måste free:a) och `http_status`. */
int geocoding_fetch_url_sync(const char* url, char** response_data,
                             int* http_status);

#endif /* GEOCODING_HTTP_H */
