#ifndef WEATHER_SERVER_INSTANCE_H
#define WEATHER_SERVER_INSTANCE_H

#include "http_server_connection.h"

typedef struct {
    HTTPServerConnection* connection;
} WeatherServerInstance;

int weather_server_instance_initiate(WeatherServerInstance* instance,
                                     HTTPServerConnection*  connection);
int weather_server_instance_initiate_ptr(HTTPServerConnection*   connection,
                                         WeatherServerInstance** instance_ptr);

void weather_server_instance_work(WeatherServerInstance* instance,
                                  uint64_t               mon_time);

void weather_server_instance_dispose(WeatherServerInstance* instance);
void weather_server_instance_dispose_ptr(WeatherServerInstance** instance_ptr);

#endif // WEATHER_SERVER_INSTANCE_H
