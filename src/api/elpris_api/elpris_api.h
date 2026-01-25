#ifndef ELPRIS_API_H
#define ELPRIS_API_H

typedef struct {
    float sek_per_kwh;
    char  time_start[128];
    char  time_end[128];
} PricePoint;

// 69 15 mintue intervals of price data from 00 to 23
typedef struct {
    PricePoint prices[96];
} ElprisResponse;

int fetch_url_sync(const char* url, char** response_data, int* http_status);

#endif // ELPRIS_API_H
