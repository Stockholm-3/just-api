/**
 * @file weather_client.h
 * @brief Asynkron väder-API-klient
 *
 * Minimal asynkron C-klient för väder-API med prognosstöd.
 */

#ifndef WEATHER_CLIENT_H
#define WEATHER_CLIENT_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Callback-funktionstyp för asynkrona vädersvar
 * @param response JSON-svarssträng (anroparen måste frigöra)
 * @param status_code HTTP-statuskod
 * @param user_data Användardefinierad kontextdata
 */
typedef void (*WeatherCallback)(char* response, int status_code,
                                void* user_data);

/**
 * @brief Förfrågningstillstånd för tillståndsmaskinarbetare
 */
typedef enum {
    REQ_STATE_IDLE = 0,
    REQ_STATE_QUEUED,
    REQ_STATE_CONNECTING,
    REQ_STATE_SENDING,
    REQ_STATE_RECEIVING,
    REQ_STATE_PROCESSING,
    REQ_STATE_COMPLETED,
    REQ_STATE_ERROR
} RequestState;

/**
 * @brief Asynkront väderförfrågningskontext
 */
typedef struct {
    char*           base_url;
    char*           endpoint;
    char*           query;
    WeatherCallback callback;
    void*           user_data;
    RequestState    state;
    uint64_t        start_time;
} WeatherRequest;

/**
 * @brief Initiera väderklienten
 * @param base_url Bas-API-URL (t.ex., "http://localhost:10680/v1")
 * @return 0 vid framgång, -1 vid fel
 */
int weather_client_init(const char* base_url);

/**
 * @brief Hämta aktuellt väder asynkront
 * @param city Stadsnamn
 * @param country_code ISO-landskod (t.ex., "SE")
 * @param callback Svars-callback
 * @param user_data Användarkontext
 * @return 0 vid framgång, -1 vid fel
 */
int weather_client_current_async(const char* city, const char* country_code,
                                 WeatherCallback callback, void* user_data);

/**
 * @brief Hämta väderprognos asynkront
 * @param city Stadsnamn
 * @param country_code ISO-landskod (t.ex., "SE")
 * @param days Antal prognodagar (1-16)
 * @param callback Svars-callback
 * @param user_data Användarkontext
 * @return 0 vid framgång, -1 vid fel
 */
int weather_client_forecast_async(const char* city, const char* country_code,
                                  int days, WeatherCallback callback,
                                  void* user_data);

/**
 * @brief Bearbeta väntande asynkrona förfrågningar
 * @return Antal bearbetade förfrågningar
 */
int weather_client_poll(void);

/**
 * @brief Tillståndsmaskinarbetare - bearbeta ett steg per förfrågan
 * @param current_time Aktuell tid i millisekunder
 * @return Antal aktiva förfrågningar
 */
int weather_client_smw_work(uint64_t current_time);

/**
 * @brief Hämta förfrågningstillståndssträng
 * @param state Förfrågningstillstånd
 * @return Läsbart tillståndsnamn
 */
const char* weather_client_get_state_name(RequestState state);

/**
 * @brief Rensa och stäng av klienten
 */
void weather_client_cleanup(void);

#endif // WEATHER_CLIENT_H
