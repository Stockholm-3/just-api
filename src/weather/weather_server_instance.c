/**
 * @file weather_server_instance.c
 * @brief Implementering av väderserverinstans och HTTP-förfrågningsrouting.
 *
 * Denna fil implementerar WeatherServerInstance-livscykelhantering och
 * HTTP-förfrågningshanteraren som router förfrågningar till lämpliga
 * endpoint-hanterare baserat på URL-sökvägen.
 *
 * @see weather_server_instance.h för det publika gränssnittet
 */

#include "weather_server_instance.h"

#include "routes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============= Interna funktionsdeklarationer ============= */

/**
 * @brief HTTP-förfrågnings-callback-hanterare.
 * @internal
 *
 * Router inkommande HTTP-förfrågningar till lämplig endpoint-hanterare
 * baserat på förfrågningsmetod och sökväg.
 *
 * @param[in] context WeatherServerInstance-pekare castad till void*.
 *
 * @return 0 vid framgång, -1 vid allvarligt fel.
 */
int weather_server_instance_on_request(void* context);

/* ============= Implementation av publikt API ============= */

/**
 * @brief Initiera en WeatherServerInstance.
 *
 * @param[in,out] instance   Instans som ska initieras.
 * @param[in]     connection HTTP-anslutning att koppla till.
 *
 * @return 0 vid framgång.
 */
int weather_server_instance_initiate(WeatherServerInstance* instance,
                                     HTTPServerConnection*  connection) {
    instance->connection = connection;

    http_server_connection_set_callback(instance->connection, instance,
                                        weather_server_instance_on_request);

    return 0;
}

/**
 * @brief Allokera och initiera en WeatherServerInstance.
 *
 * @param[in]  connection   HTTP-anslutning som ska hanteras.
 * @param[out] instance_ptr Pekare som tar emot den allokerade instansen.
 *
 * @return 0 vid framgång, -1 om instance_ptr är NULL, -2 om allokering misslyckas.
 */
int weather_server_instance_initiate_ptr(HTTPServerConnection*   connection,
                                         WeatherServerInstance** instance_ptr) {
    if (instance_ptr == NULL) {
        return -1;
    }

    WeatherServerInstance* instance =
        (WeatherServerInstance*)malloc(sizeof(WeatherServerInstance));
    if (instance == NULL) {
        return -2;
    }

    int result = weather_server_instance_initiate(instance, connection);
    if (result != 0) {
        free(instance);
        return result;
    }

    *(instance_ptr) = instance;

    return 0;
}

/**
 * @brief Callback från http_server_connection som hanterar alla routes
 */
int weather_server_instance_on_request(void* context) {
    WeatherServerInstance* inst = (WeatherServerInstance*)context;
    HTTPServerConnection*  conn = inst->connection;

    char path[256]  = {0};
    char query[512] = {0};

    split_path_and_query(conn->request_path, path, sizeof(path), query,
                         sizeof(query));

    for (size_t i = 0; i < ROUTE_COUNT; i++) {
        if (strcmp(conn->method, g_routes[i].method) == 0 &&
            strcmp(path, g_routes[i].path) == 0) {
            return g_routes[i].handler(conn, query);
        }
    }

    return handle_not_found(conn, path);
}

/* ============= Livscykelfunktioner ============= */

/**
 * @brief Periodisk arbetsfunktion (för närvarande no-op).
 *
 * @param[in] instance Instans att bearbeta.
 * @param[in] mon_time Aktuell schemaläggartid.
 */
void weather_server_instance_work(WeatherServerInstance* instance,
                                  uint64_t               mon_time) {
    /* Reserverad för framtida timeout/rensningslogik */
}

/**
 * @brief Avsluta en stackallokerad instans (för närvarande no-op).
 *
 * @param[in] instance Instans som ska avslutas.
 */
void weather_server_instance_dispose(WeatherServerInstance* instance) {
    /* Reserverad för framtida rensningslogik */
}

/**
 * @brief Avsluta och frigör en dynamiskt allokerad instans.
 *
 * @param[in,out] instance_ptr Pekare till instanspekaren (sätts till NULL).
 */
void weather_server_instance_dispose_ptr(WeatherServerInstance** instance_ptr) {
    if (instance_ptr == NULL || *(instance_ptr) == NULL) {
        return;
    }

    weather_server_instance_dispose(*(instance_ptr));
    free(*(instance_ptr));
    *(instance_ptr) = NULL;
}
