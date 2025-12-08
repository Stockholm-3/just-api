#ifndef WEATHER_SERVER_H
#define WEATHER_SERVER_H

#include "http_server.h"
#include "linked_list.h"
#include "smw.h"

typedef struct {
    HTTPServer httpServer;

    LinkedList* instances;

    SmwTask* task;

} WeatherServer;

int weather_server_initiate(WeatherServer* server);
int weather_server_initiate_ptr(WeatherServer** server_ptr);

void weather_server_dispose(WeatherServer* server);
void weather_server_dispose_ptr(WeatherServer** server_ptr);

#endif // WEATHER_SERVER_H
