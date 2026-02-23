/**
 * @file weather_server.c
 * @brief Implementering av väder-HTTP-servern.
 *
 * Denna fil implementerar WeatherServer-livscykelhantering och
 * interna callback-funktioner för hantering av HTTP-anslutningar.
 *
 * @see weather_server.h för det publika gränssnittet
 */

#include "weather_server.h"

#include "weather_server_instance.h"

#include <stdio.h>
#include <stdlib.h>

/* ============= Interna funktionsdeklarationer ============= */

/**
 * @brief Schemaläggningscallback för periodiskt instansarbete.
 * @internal
 *
 * Anropas periodiskt av schemaläggaren för att utföra underhållsarbete
 * på alla aktiva serverinstanser.
 *
 * @param[in] context  WeatherServer-pekare castad till void*.
 * @param[in] mon_time Aktuell schemaläggartid i tick.
 */
void weather_server_task_work(void* context, uint64_t mon_time);

/**
 * @brief HTTP-anslutningscallback för nya klientanslutningar.
 * @internal
 *
 * Anropas av HTTP-servern när en ny klient ansluter.
 * Skapar en ny WeatherServerInstance för att hantera anslutningen.
 *
 * @param[in] context    WeatherServer-pekare castad till void*.
 * @param[in] connection Den nya HTTP-anslutningen som ska hanteras.
 *
 * @return 0 vid framgång, -1 vid fel.
 */
int weather_server_on_http_connection(void*                 context,
                                      HTTPServerConnection* connection);

/* ============= Implementation av publikt API ============= */

/**
 * @brief Initiera en WeatherServer-struktur.
 *
 * @param[in,out] server Server som ska initieras.
 *
 * @return 0 vid framgång.
 */
int weather_server_initiate(WeatherServer* server) {
    http_server_initiate(&server->httpServer,
                         weather_server_on_http_connection);

    server->instances = linked_list_create();

    server->task = smw_create_task(server, weather_server_task_work);

    return 0;
}

/**
 * @brief Allokera och initiera en WeatherServer dynamiskt.
 *
 * @param[out] server_ptr Pekare som tar emot den allokerade servern.
 *
 * @return 0 vid framgång, -1 om server_ptr är NULL, -2 om allokering misslyckas.
 */
int weather_server_initiate_ptr(WeatherServer** server_ptr) {
    if (server_ptr == NULL) {
        return -1;
    }

    WeatherServer* server = (WeatherServer*)malloc(sizeof(WeatherServer));
    if (server == NULL) {
        return -2;
    }

    int result = weather_server_initiate(server);
    if (result != 0) {
        free(server);
        return result;
    }

    *(server_ptr) = server;

    return 0;
}

/* ============= Implementering av interna callbacks ============= */

/**
 * @brief Hantera ny HTTP-anslutning genom att skapa en serverinstans.
 * @internal
 *
 * @param[in] context    WeatherServer-pekare.
 * @param[in] connection Ny HTTP-anslutning.
 *
 * @return 0 vid framgång, -1 vid fel.
 */
int weather_server_on_http_connection(void*                 context,
                                      HTTPServerConnection* connection) {
    WeatherServer* server = (WeatherServer*)context;

    WeatherServerInstance* instance = NULL;
    int result = weather_server_instance_initiate_ptr(connection, &instance);
    if (result != 0) {
        printf("WeatherServer_OnHTTPConnection: Failed to initiate instance\n");
        return -1;
    }

    linked_list_append(server->instances, instance);

    return 0;
}

/**
 * @brief Periodisk arbets-callback för alla serverinstanser.
 * @internal
 *
 * Itererar igenom alla aktiva instanser och anropar deras arbetsfunktion.
 *
 * @param[in] context  WeatherServer-pekare.
 * @param[in] mon_time Aktuell schemaläggartid.
 */
void weather_server_task_work(void* context, uint64_t mon_time) {
    WeatherServer* server = (WeatherServer*)context;

    LinkedList_foreach(server->instances, node) {
        WeatherServerInstance* instance = (WeatherServerInstance*)node->item;
        weather_server_instance_work(instance, mon_time);
    }
}

/* ============= Rensningsfunktioner ============= */

/**
 * @brief Avsluta en stackallokerad WeatherServer.
 *
 * @param[in] server Server som ska avslutas.
 */
void weather_server_dispose(WeatherServer* server) {
    /* Rensa alla instanser för att förhindra minnesläcka */
    LinkedList_foreach(server->instances, node) {
        WeatherServerInstance* instance = (WeatherServerInstance*)node->item;
        weather_server_instance_dispose(instance);
    }
    linked_list_dispose(&server->instances, free);

    http_server_dispose(&server->httpServer);
    smw_destroy_task(server->task);
}

/**
 * @brief Avsluta och frigör en dynamiskt allokerad WeatherServer.
 *
 * @param[in,out] server_ptr Pekare till serverpekaren (sätts till NULL).
 */
void weather_server_dispose_ptr(WeatherServer** server_ptr) {
    if (server_ptr == NULL || *(server_ptr) == NULL) {
        return;
    }

    weather_server_dispose(*(server_ptr));
    free(*(server_ptr));
    *(server_ptr) = NULL;
}
