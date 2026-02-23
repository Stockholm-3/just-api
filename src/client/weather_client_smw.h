/**
 * @file weather_client_smw.h
 * @brief Tillståndsmaskinarbetare för asynkrona väderklientförfrågningar
 */

#ifndef WEATHER_CLIENT_SMW_H
#define WEATHER_CLIENT_SMW_H

#include "weather_client.h"

/**
 * @brief Tillståndsmaskinarbetare - bearbeta ett steg per förfrågan
 *
 * Itererar genom alla köade förfrågningar och avancerar deras tillståndsmaskin.
 * Varje anrop bearbetar en tillståndsövergång per förfrågan, vilket möjliggör
 * steg-för-steg asynkron körning med full synlighet i förfrågningens livscykel.
 *
 * @param requests Array av väderförfrågningar
 * @param request_count Antal förfrågningar i arrayen
 * @param current_time Aktuell tid i millisekunder (för tidsmätning)
 * @param http_executor Funktionspekare för att utföra HTTP-förfrågningar
 * @return Antal aktiva (ej avslutade) förfrågningar
 */
int weather_client_smw_work_impl(WeatherRequest* requests, int request_count,
                                 uint64_t current_time,
                                 char* (*http_executor)(const char*, int*));

/**
 * @brief Hämta läsbart tillståndsnamn
 * @param state Förfrågningstillstånd
 * @return Tillståndsnamnsträng
 */
const char* weather_client_get_state_name(RequestState state);

#endif // WEATHER_CLIENT_SMW_H
