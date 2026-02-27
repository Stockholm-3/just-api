#include "../include/Cache.hpp"
#include "../include/Config.hpp"
#include "../include/EnergyPlan.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::string defaultBaseUrl = "https://api.example.com";
    std::string       configuredBaseUrl;

    if (argc > 1 && argv[1] != nullptr && std::string(argv[1]).size() > 0U) {
        configuredBaseUrl = argv[1];
    } else if (const char* envBaseUrl = std::getenv("JUST_API_BASE_URL");
               envBaseUrl != nullptr && std::string(envBaseUrl).size() > 0U) {
        configuredBaseUrl = envBaseUrl;
    } else {
        configuredBaseUrl = defaultBaseUrl;
    }

    Config config(configuredBaseUrl, 300);
    Cache  cache;
    cache.put("last_city", "Stockholm", config.cacheTtlSeconds());

    EnergyPlan          energyPlan;
    std::vector<double> solarForecast(24, 0.5);
    auto                plan = energyPlan.generatePlan(1.2, solarForecast, 3.0);

    std::cout << "Config base URL: " << config.baseUrl() << std::endl;
    std::cout << "Cache entries: " << cache.size() << std::endl;
    std::cout << "Total energy needed (kWh): " << energyPlan.totalKwh(plan)
              << std::endl;

    return 0;
}