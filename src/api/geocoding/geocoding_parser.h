/**
 * geocoding_parser.h - JSON-parsning för geokodningssvar
 */

#ifndef GEOCODING_PARSER_H
#define GEOCODING_PARSER_H

#include <geocoding_api.h>

/* Parsar en JSON-sträng från geokodnings-API:t till en
 * GeocodingResponse. Anropande kod måste frigöra svaret med
 * geocoding_api_free_response().
 * Returnerar 0 vid framgång, <0 vid fel. */
int geocoding_parse_json(const char* json_str, GeocodingResponse** response);

#endif /* GEOCODING_PARSER_H */
