#include "elPrisparser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

float* parse_elpriser_fran_json(const char* json_string, int* antal_priser) {
    if (json_string == NULL) {
        printf("Fel: NULL JSON-sträng\n");
        *antal_priser = 0;
        return NULL;
    }
    

    int count = 0;
    const char* ptr = json_string;
    while ((ptr = strstr(ptr, "SEK_per_kWh")) != NULL) {
        count++;
        ptr += 12;
    }
    
    if (count == 0) {
        printf("Fel: Inga priser hittades i JSON\n");
        *antal_priser = 0;
        return NULL;
    }
    
    
    float* priser = malloc(count * sizeof(float));
    if (priser == NULL) {
        printf("Fel: Kunde inte allokera minne\n");
        *antal_priser = 0;
        return NULL;
    }
    
    
    ptr = json_string;
    int index = 0;
    
    while (index < count && ptr != NULL) {
        // Hitta "SEK_per_kWh":
        ptr = strstr(ptr, "SEK_per_kWh");
        if (ptr == NULL) break;
        
        // Hitta kolon efter "SEK_per_kWh"
        ptr = strchr(ptr, ':');
        if (ptr == NULL) break;
        
        // Hoppa till värdet
        ptr++;
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        
        // Läs in priset
        priser[index] = atof(ptr);
        index++;
        
        // Fortsätt leta efter nästa
        ptr++;
    }
    
    *antal_priser = count;
    return priser;
}