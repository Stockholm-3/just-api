#ifndef ENERGY_PLAN_HPP
#define ENERGY_PLAN_HPP

#include <vector>

class EnergyPlan {
public:
    EnergyPlan();
    EnergyPlan(const EnergyPlan& other);
    EnergyPlan& operator=(const EnergyPlan& other);
    ~EnergyPlan();

    struct HourSlot {
        int hour;
        double kwh;
    };

    std::vector<HourSlot> generatePlan(double baseLoadKwh,
                                       const std::vector<double>& solarForecastKwh,
                                       double batteryCapacityKwh) const;
    double totalKwh(const std::vector<HourSlot>& plan) const;
};

#endif
