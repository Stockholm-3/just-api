/**
 * @file weather_server_instance.h
 * @brief Hantering av väderserverinstanser för HTTP-anslutningar.
 *
 * Denna modul tillhandahåller livscykelhantering för enskilda väderserver-
 * instanser. Varje WeatherServerInstance omsluter en HTTPServerConnection och
 * hanterar HTTP-förfrågningsbearbetning för väderrelaterade endpoints.
 *
 * Modulen stöder både stackallokerade och heapallokerade instanser,
 * med motsvarande initierings- och rensningsfunktioner för varje
 * allokeringsmetod.
 *
 * @par Stödda endpoints:
 * - GET / - Startsida med API-dokumentation
 * - GET/POST /echo - Echo-endpoint för felsökning
 * - GET /v1/current?lat=XX&lon=YY - Aktuellt väder via koordinater
 * - GET /v1/weather?city=NAME&country=CODE - Väder via stadsnamn
 * - GET /v1/cities?query=SEARCH - Stadssökning för autocomplete
 *
 * @note Instanser måste initieras korrekt innan användning och avslutas
 *       när de inte längre behövs för att förhindra resursläckor.
 *
 * @see http_server_connection.h för den underliggande anslutningshanteringen
 * @see weather_server.h för den överordnade servern som hanterar instanser
 */

#ifndef WEATHER_SERVER_INSTANCE_H
#define WEATHER_SERVER_INSTANCE_H

#include "http_server_connection.h"

/**
 * @brief Väderserverinstans för hantering av en enskild HTTP-anslutning.
 *
 * Omsluter en HTTPServerConnection och tillhandahåller förfrågningsrouting
 * och responsgenerering för väder-API-endpoints.
 */
typedef struct {
    /** @brief Pekare till den underliggande HTTP-anslutningen. */
    HTTPServerConnection* connection;
} WeatherServerInstance;

/**
 * @brief Initiera en stackallokerad WeatherServerInstance.
 *
 * Kopplar instansen till den angivna HTTP-anslutningen och registrerar
 * förfrågnings-callback-hanteraren för bearbetning av inkommande HTTP-förfrågningar.
 *
 * @param[in,out] instance   Pekare till WeatherServerInstance som ska initieras.
 *                           Måste vara giltig, icke-NULL minnesadress.
 * @param[in]     connection Pekare till HTTPServerConnection som ska hanteras.
 *                           Instansen tar inte äganderätt.
 *
 * @return 0 vid framgång, icke-noll vid fel.
 *
 * @note Instansen måste avslutas med weather_server_instance_dispose()
 *       när den inte längre behövs.
 *
 * @par Exempel:
 * @code{.c}
 * WeatherServerInstance instance;
 * if (weather_server_instance_initiate(&instance, connection) == 0) {
 *     // Instansen är redo att hantera förfrågningar
 *     weather_server_instance_dispose(&instance);
 * }
 * @endcode
 */
int weather_server_instance_initiate(WeatherServerInstance* instance,
                                     HTTPServerConnection*  connection);

/**
 * @brief Allokera och initiera en WeatherServerInstance dynamiskt.
 *
 * Allokerar minne för en WeatherServerInstance, initierar den med
 * weather_server_instance_initiate() och sätter utdatapekaren.
 * Vid misslyckad initiering frigörs det allokerade minnet automatiskt.
 *
 * @param[in]  connection   Pekare till HTTPServerConnection som ska hanteras.
 * @param[out] instance_ptr Pekare som tar emot den allokerade instansen.
 *                          Sätts till den nya instansen vid framgång.
 *
 * @return
 * - 0 vid framgång
 * - -1 om instance_ptr är NULL
 * - -2 om minnesallokering misslyckas
 * - Andra icke-nollvärden från weather_server_instance_initiate()
 *
 * @note Instansen måste avslutas med
 * weather_server_instance_dispose_ptr() när den inte längre behövs.
 *
 * @par Exempel:
 * @code{.c}
 * WeatherServerInstance* instance = NULL;
 * if (weather_server_instance_initiate_ptr(connection, &instance) == 0) {
 *     // Instansen är redo
 *     weather_server_instance_dispose_ptr(&instance);
 * }
 * @endcode
 */
int weather_server_instance_initiate_ptr(HTTPServerConnection*   connection,
                                         WeatherServerInstance** instance_ptr);

/**
 * @brief Periodisk arbetsfunktion för instansen.
 *
 * Anropas av WeatherServer-schemaläggaren för varje aktiv instans.
 * Kan användas för att implementera timeouts, anslutningsrensning eller
 * andra periodiska underhållsuppgifter.
 *
 * @param[in] instance Pekare till WeatherServerInstance.
 * @param[in] mon_time Aktuell schemaläggartid i tick.
 *
 * @note Är för närvarande en no-op, reserverad för framtida funktionalitet
 *       såsom förfrågnings-timeouts eller keep-alive-hantering.
 */
void weather_server_instance_work(WeatherServerInstance* instance,
                                  uint64_t               mon_time);

/**
 * @brief Avsluta en stackallokerad WeatherServerInstance.
 *
 * Frigör resurser som ägs av instansen, såsom tillfälliga buffertar
 * eller timers. Den underliggande anslutningen avslutas inte
 * (den ägs av HTTP-servern).
 *
 * @param[in] instance Pekare till WeatherServerInstance som ska avslutas.
 *
 * @note Är för närvarande en no-op, reserverad för framtida rensningslogik.
 * @note Efter anrop till denna funktion bör instansen inte användas
 *       utan att återinitieras.
 */
void weather_server_instance_dispose(WeatherServerInstance* instance);

/**
 * @brief Avsluta och frigör en dynamiskt allokerad WeatherServerInstance.
 *
 * Anropar weather_server_instance_dispose() för att rensa instansen,
 * frigör sedan det allokerade minnet och sätter pekaren till NULL.
 *
 * @param[in,out] instance_ptr Pekare till WeatherServerInstance-pekaren.
 *                             Den pekade pekaren sätts till NULL efter avslut.
 *                             Säkert att anropa med NULL eller pekare till NULL.
 *
 * @par Exempel:
 * @code{.c}
 * WeatherServerInstance* instance = NULL;
 * weather_server_instance_initiate_ptr(connection, &instance);
 * // Använd instansen...
 * weather_server_instance_dispose_ptr(&instance);
 * // instance är nu NULL
 * @endcode
 */
void weather_server_instance_dispose_ptr(WeatherServerInstance** instance_ptr);

#endif /* WEATHER_SERVER_INSTANCE_H */
