#include <iostream>
#include <string>
#include <vector>

#include "Cache.hpp"
#include "Config.hpp"
#include "EnergyPlan.hpp"

int main() {
    Config config("https://api.example.com", 300);
    Cache cache;
    cache.put("last_city", "Stockholm", config.cacheTtlSeconds());

    EnergyPlan energyPlan;
    std::vector<double> solarForecast(24, 0.5);
    auto plan = energyPlan.generatePlan(1.2, solarForecast, 3.0);

    std::cout << "Config base URL: " << config.baseUrl() << std::endl;
    std::cout << "Cache entries: " << cache.size() << std::endl;
    std::cout << "Total energy needed (kWh): " << energyPlan.totalKwh(plan) << std::endl;

    return 0;
}