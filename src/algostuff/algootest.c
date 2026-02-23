#include "elPrisparser.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char*  read_json_from_file(const char* filename);
int    get_current_15min_index(void);
float  get_value_by_index(float* array, int array_size, int index);
int    sell_or_not(float current_price, float production_kwh_min);
float  estimate_solar_production(int hour, float current_temp);
int    calculate_current_index(void);
float* extract_temperatures(char* json_data, int* num_temps);

// Wrapper function that does all the work
int run_weather_analysis(const char* price_file, const char* weather_file) {
    time_t     now     = time(NULL);
    struct tm* tm_info = localtime(&now);
    int        hour    = tm_info->tm_hour;

    // Read price data
    char* json_data = read_json_from_file(price_file);
    if (!json_data) {
        printf("Kunde inte läsa JSON-data från %s\n", price_file);
        return 1;
    }

    int    antal_priser;
    float* priser = parse_elpriser_fran_json(json_data, &antal_priser);
    free(json_data);

    if (priser == NULL || antal_priser <= 0) {
        printf("Kunde inte parsa elpriser!\n");
        return 1;
    }

    // Read weather data
    char* tmp_json = read_json_from_file(weather_file);
    if (!tmp_json) {
        printf("Kunde inte läsa JSON-data från %s\n", weather_file);
        free(priser);
        return 1;
    }

    int    num_temps;
    float* weather_data = extract_temperatures(tmp_json, &num_temps);
    free(tmp_json);

    if (weather_data == NULL || num_temps <= 0) {
        printf("Kunde inte parsa temperaturer!\n");
        free(priser);
        return 1;
    }

    // Calculate current values
    int   index         = calculate_current_index();
    float current_price = get_value_by_index(priser, antal_priser, index);
    float current_temp  = get_value_by_index(weather_data, num_temps, index);

    // Print results
    printf("\n=== RESULTAT ===\n");
    printf("Aktuell tid: %02d:%02d\n", tm_info->tm_hour, tm_info->tm_min);
    printf("Index i array: %d\n", index);
    printf("Aktuellt elpris: %.2f SEK/kWh\n", current_price);
    printf("Aktuell temperatur: %.1f°C\n", current_temp);

    // Calculate production and decision
    float production_kwh_min = estimate_solar_production(hour, current_temp);
    int   sell               = sell_or_not(current_price, production_kwh_min);

    if (sell) {
        printf("\n✓ REKOMMENDATION: Sälj el just nu!\n");
    } else {
        printf("\n✗ REKOMMENDATION: Sälj INTE el just nu!\n");
    }

    // Cleanup
    free(priser);
    free(weather_data);

    return sell ? 0 : 1;
}

int main() { return run_weather_analysis("elPrisdata.txt", "vaderData.txt"); }

char* read_json_from_file(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        printf("Error: Kunde inte öppna filen %s\n", filename);
        return NULL;
    }

    // Gå till slutet av filen för att få storleken
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Allokera minne för hela filen + null-terminator
    char* buffer = (char*)malloc(file_size + 1);
    if (!buffer) {
        printf("Error: Kunde inte allokera minne\n");
        fclose(file);
        return NULL;
    }

    // Läs hela filen
    size_t bytes_read  = fread(buffer, 1, file_size, file);
    buffer[bytes_read] = '\0'; // Null-terminera

    fclose(file);
    printf("Läste %ld bytes från %s\n", file_size, filename);

    return buffer;
}

int get_current_15min_index(void) {
    time_t     now     = time(NULL);
    struct tm* tm_info = localtime(&now);

    int hour   = tm_info->tm_hour;
    int minute = tm_info->tm_min;

    return (hour * 4) + (minute / 15);
}

int calculate_current_index(void) { return get_current_15min_index(); }

float get_value_by_index(float* array, int array_size, int index) {
    if (index < 0) {
        index = 0;
    }
    if (index >= array_size) {
        index = array_size - 1;
    }

    return array[index];
}

float estimate_solar_production(int hour, float current_temp) {
    float max_production = 0.006667f; // 400W panel = 0.4kW/60min

    // 1. TIDSPÅVERKAN (solens position)
    float time_factor = 0.0f;
    if (hour >= 6 && hour <= 18) {
        // Sinuskurva med topp vid 12:00
        float hour_of_day = hour - 6; // 0-12
        time_factor       = sin(hour_of_day * 3.14159f / 12.0f);
        if (time_factor < 0) {
            time_factor = 0;
        }
    }

    // 2. TEMPERATURPÅVERKAN (solpaneler tappar effekt vid höga temp)
    float temp_factor = 1.0f;
    if (current_temp > 25.0f) {
        // -0.4% per grad över 25°C
        temp_factor = 1.0f - ((current_temp - 25.0f) * 0.004f);
        if (temp_factor < 0.7f) {
            temp_factor = 0.7f; // Minst 70% kvar
        }
    }

    // Total produktion
    return max_production * time_factor * temp_factor;
}

int sell_or_not(float current_price, float production_kwh_min) {
    // Konvertera produktionen till kronor per minut
    float value_per_min = production_kwh_min * current_price;

    printf("\n--- BERÄKNING ---\n");
    printf("Produktion: %.6f kWh/min\n", production_kwh_min);
    printf("Spotpris: %.5f SEK/kWh\n", current_price);
    printf("Värde av produktionen: %.4f SEK/min\n", value_per_min);

    // Sätt ett tröskelvärde - t.ex. 0.01 SEK/min (60 öre per timme)
    float threshold_value = 0.01f; // SEK/min

    if (value_per_min > threshold_value) {
        printf("✓ SÄLJ: Värdet (%.4f SEK/min) > tröskel (%.3f SEK/min)\n",
               value_per_min, threshold_value);
        return 1;
    } else {
        printf("✗ SÄLJ INTE: Värdet (%.4f SEK/min) <= tröskel (%.3f SEK/min)\n",
               value_per_min, threshold_value);
        return 0;
    }
}

float* extract_temperatures(char* json_data, int* num_temps) {
    if (!json_data) {
        printf("Error: JSON-data är NULL\n");
        return NULL;
    }

    printf("Letar efter hourly.temperature_2m...\n");

    // Hitta "hourly" sektionen först
    char* hourly_start = strstr(json_data, "\"hourly\"");
    if (!hourly_start) {
        printf("Error: Hittade inte 'hourly' i JSON\n");
        return NULL;
    }

    // Inuti hourly, hitta "temperature_2m"
    char* temp_start = strstr(hourly_start, "\"temperature_2m\"");
    if (!temp_start) {
        printf("Error: Hittade inte 'temperature_2m' i hourly\n");
        return NULL;
    }

    // Hitta början av arrayen
    temp_start = strchr(temp_start, '[');
    if (!temp_start) {
        printf("Error: Hittade inte starten av temperatur-arrayen\n");
        return NULL;
    }
    temp_start++; // Hoppa över '['

    // Räkna antal temperaturer
    int   count     = 0;
    char* p         = temp_start;
    int   in_number = 0;

    while (*p && *p != ']') {
        if (isdigit(*p) || *p == '-' || *p == '.') {
            if (!in_number) {
                count++;
                in_number = 1;
            }
        } else {
            in_number = 0;
        }
        p++;
    }

    printf("Hittade %d temperaturvärden\n", count);

    if (count == 0) {
        printf("Error: Inga temperaturer hittades\n");
        return NULL;
    }

    // Allokera array
    float* temps = (float*)malloc(count * sizeof(float));
    if (!temps) {
        printf("Error: Kunde inte allokera minne\n");
        return NULL;
    }

    // Extrahera temperaturer
    p          = temp_start;
    int  index = 0;
    char num_buffer[32];
    int  num_pos = 0;
    in_number    = 0;

    while (*p && *p != ']' && index < count) {
        if (isdigit(*p) || *p == '-' || *p == '.') {
            num_buffer[num_pos++] = *p;
            in_number             = 1;
        } else {
            if (in_number) {
                num_buffer[num_pos] = '\0';
                temps[index++]      = (float)atof(num_buffer);
                num_pos             = 0;
                in_number           = 0;
            }
        }
        p++;
    }

    // Fånga sista numret om det behövs
    if (in_number && index < count) {
        num_buffer[num_pos] = '\0';
        temps[index++]      = (float)atof(num_buffer);
    }

    *num_temps = count;
    printf("Extraherade %d temperaturer\n", index);

    return temps;
}
