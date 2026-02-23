/**
 * @file weather_server.h
 * @brief Huvudmodul för väder-HTTP-servern.
 *
 * Denna modul tillhandahåller WeatherServer-strukturen och
 * livscykelhanteringsfunktioner. WeatherServer omsluter en HTTP-server och
 * hanterar flera klientanslutningar via WeatherServerInstance-objekt.
 *
 * @par Arkitektur:
 * - WeatherServer innehåller en HTTPServer för hantering av TCP-anslutningar
 * - Varje klientanslutning skapar en WeatherServerInstance
 * - En schemaläggare utför periodiskt arbete på alla aktiva instanser
 * - Instanser lagras i en länkad lista för iteration
 *
 * @par Användning:
 * @code{.c}
 * WeatherServer* server = NULL;
 * if (weather_server_initiate_ptr(&server) == 0) {
 *     // Servern är igång, hantera händelser...
 *     weather_server_dispose_ptr(&server);
 * }
 * @endcode
 *
 * @see weather_server_instance.h för hantering av enskilda anslutningar
 * @see http_server.h för den underliggande HTTP-serverimplementationen
 */

#ifndef WEATHER_SERVER_H
#define WEATHER_SERVER_H

#include "http_server.h"
#include "linked_list.h"
#include "smw.h"

/**
 * @brief Huvudstruktur för väderservern.
 *
 * Innehåller HTTP-servern, anslutningsinstanser och schemaläggningsuppgift
 * för hantering av väder-API-servern.
 */
typedef struct {
    /** @brief Inbäddad HTTP-server för hantering av anslutningar. */
    HTTPServer httpServer;

    /** @brief Länkad lista med aktiva WeatherServerInstance-pekare. */
    LinkedList* instances;

    /** @brief Schemaläggningsuppgift för periodiskt instansarbete. */
    SmwTask* task;

} WeatherServer;

/**
 * @brief Initiera en stackallokerad WeatherServer.
 *
 * Utför fullständig initiering av väderservern:
 * - Initierar den inbäddade HTTP-servern med anslutningscallback
 * - Skapar en tom länkad lista för klientinstanser
 * - Registrerar en schemaläggningsuppgift för periodiskt arbete
 *
 * @param[in,out] server Pekare till WeatherServer-strukturen som ska initieras.
 *                       Måste vara giltig, icke-NULL minnesadress.
 *
 * @return 0 vid framgång, icke-noll vid fel.
 *
 * @note Servern måste avslutas med weather_server_dispose() när den är klar.
 *
 * @par Exempel:
 * @code{.c}
 * WeatherServer server;
 * if (weather_server_initiate(&server) == 0) {
 *     // Använd servern...
 *     weather_server_dispose(&server);
 * }
 * @endcode
 */
int weather_server_initiate(WeatherServer* server);

/**
 * @brief Allokera och initiera en WeatherServer dynamiskt.
 *
 * Allokerar minne för en WeatherServer och initierar den med
 * weather_server_initiate(). Vid misslyckad initiering frigörs
 * det allokerade minnet automatiskt.
 *
 * @param[out] server_ptr Pekare som tar emot den allokerade servern.
 *                        Sätts till den nya servern vid framgång, oförändrad
 * vid fel.
 *
 * @return
 * - 0 vid framgång
 * - -1 om server_ptr är NULL
 * - -2 om minnesallokering misslyckas
 * - Andra icke-nollvärden från weather_server_initiate() vid initieringsfel
 *
 * @note Servern måste avslutas med weather_server_dispose_ptr() när den är
 * klar.
 *
 * @par Exempel:
 * @code{.c}
 * WeatherServer* server = NULL;
 * int result = weather_server_initiate_ptr(&server);
 * if (result == 0) {
 *     // Använd servern...
 *     weather_server_dispose_ptr(&server);
 * }
 * @endcode
 */
int weather_server_initiate_ptr(WeatherServer** server_ptr);

/**
 * @brief Stäng av och rensa upp en stackallokerad WeatherServer.
 *
 * Utför fullständig rensning av väderservern:
 * - Avslutar alla aktiva klientinstanser
 * - Frigör instansernas länkade lista
 * - Stoppar och avslutar HTTP-servern
 * - Förstör schemaläggningsuppgiften
 *
 * @param[in] server Pekare till WeatherServer som ska avslutas.
 *                   Måste ha initierats med weather_server_initiate().
 *
 * @note Efter anrop till denna funktion bör serverstrukturen inte användas
 *       utan att återinitieras.
 */
void weather_server_dispose(WeatherServer* server);

/**
 * @brief Avsluta och frigör en dynamiskt allokerad WeatherServer.
 *
 * Anropar weather_server_dispose() för att rensa servern, frigör sedan
 * det allokerade minnet och sätter pekaren till NULL.
 *
 * @param[in,out] server_ptr Pekare till WeatherServer-pekaren.
 *                           Den pekade pekaren sätts till NULL efter avslut.
 *                           Säkert att anropa med NULL eller pekare till NULL.
 *
 * @par Exempel:
 * @code{.c}
 * WeatherServer* server = NULL;
 * weather_server_initiate_ptr(&server);
 * // Använd servern...
 * weather_server_dispose_ptr(&server);
 * // server är nu NULL
 * @endcode
 */
void weather_server_dispose_ptr(WeatherServer** server_ptr);

#endif /* WEATHER_SERVER_H */
