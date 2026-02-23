/**
 * @file elpris_api.h
 * @brief Asynkront HTTP-API för hämtning av svenska elpriser.
 *
 * Denna modul exponerar en enda ingångspunkt som:
 *  - Parsar HTTP-frågeparametrar
 *  - Bestämmer korrekt prisdatum med svenska tidszonsregler
 *  - Hämtar elpriser från det publika elprisetjustnu.se API:t
 *  - Använder filbaserad caching för både senaste och historiska data
 *  - Skickar ett HTTP-svar asynkront via den angivna serveranslutningen
 *
 * API:t är designat för att anropas från en HTTP-serverförfrågningshanterare.
 */

#ifndef ELPRIS_API_H
#define ELPRIS_API_H

#include <http_server_connection.h>

/**
 * @brief Hämta elpriser och svara asynkront via HTTP.
 *
 * Denna funktion parsar den angivna frågesträngen, bestämmer korrekt
 * prisdatum och antingen serverar cachad data eller hämtar färsk data
 * från elprisens uppströms-API.
 *
 * ## Frågeparametrar
 * Följande frågeparametrar stöds:
 *
 * - `price` (obligatorisk):
 *   Elpriszonen (t.ex. `"SE1"`, `"SE2"`, `"SE3"`, `"SE4"`).
 *
 * - `date` (valfri):
 *   Datum i ISO-format `YYYY-MM-DD`.
 *   Om utelämnad väljer funktionen automatiskt det "senast tillgängliga"
 *   prisdatumet enligt svensk lokaltid:
 *     - Före 13:00 CET/CEST → idag
 *     - Från 13:00 och framåt → imorgon
 *
 * ## Cachningsbeteende
 * - Senaste prisdata cachas till nästa svenska 13:00-gräns.
 * - Historisk prisdata cachas under lång tid (i praktiken permanent).
 *
 * ## Svarsbeteende
 * - Vid framgång skickas ett `200 OK`-svar med `application/json`-innehåll.
 * - Vid klientfel (ogiltig fråga) skickas ett `400 Bad Request` JSON-fel.
 * - Vid uppströms- eller interna fel skickas ett `503 Service Unavailable`
 * JSON-fel.
 *
 * ## Asynkron semantik
 * - HTTP-förfrågan till uppströms-API:t utförs asynkront.
 * - Alla svar (framgång eller fel) skickas från den asynkrona callbacken.
 * - Anroparen får inte återanvända eller frigöra `HTTPServerConnection`
 *   förrän ett svar har skickats.
 *
 * @param conn
 *   Aktiv HTTP-serveranslutning som används för att skicka svaret.
 *   Får inte vara NULL.
 *
 * @param query
 *   Rå frågesträng från HTTP-förfrågan.
 *   Kan vara NULL eller tom (behandlas som ogiltig).
 *   Exempel: `"?price=SE3&date=2024-01-15"`
 *
 * @return
 *   0 vid lyckad initiering av förfrågan (inklusive cache-träffar).
 *  -1 om ett omedelbart fel inträffar (ogiltig indata, minnesallokeringsfel
 *     eller oförmåga att starta HTTP-förfrågan).
 *   Obs: asynkrona fel rapporteras via HTTP-felsvar, inte via detta returvärde.
 */
int elpris_api_fetch_and_respond(HTTPServerConnection* conn, const char* query);

#endif /* ELPRIS_API_H */
