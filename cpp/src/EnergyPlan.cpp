#include "../include/EnergyPlan.hpp"

EnergyPlan::EnergyPlan() = default;

EnergyPlan::EnergyPlan(const EnergyPlan& other) = default;

EnergyPlan& EnergyPlan::operator=(const EnergyPlan& other) = default;

EnergyPlan::~EnergyPlan() = default;

std::vector<EnergyPlan::HourSlot> EnergyPlan::generatePlan(
    double baseLoadKwh,
    const std::vector<double>& solarForecastKwh,
    double batteryCapacityKwh) const {
    std::vector<HourSlot> plan;
    plan.reserve(24);

    double batteryRemaining = batteryCapacityKwh;

    for (int hour = 0; hour < 24; ++hour) {
        double solar = (hour < static_cast<int>(solarForecastKwh.size())) 
                       ? solarForecastKwh[hour] : 0.0;
        double net = baseLoadKwh - solar;

        if (net > 0.0 && batteryRemaining > 0.0) {
            double discharge = (net < batteryRemaining) ? net : batteryRemaining;
            net -= discharge;
            batteryRemaining -= discharge;
        } else if (net < 0.0 && batteryRemaining < batteryCapacityKwh) {
            double capacityLeft = batteryCapacityKwh - batteryRemaining;
            double charge = (-net < capacityLeft) ? -net : capacityLeft;
            net += charge;
            batteryRemaining += charge;
        }

        HourSlot slot;
        slot.hour = hour;
        slot.kwh = net;
        plan.push_back(slot);
    }

    return plan;
}

double EnergyPlan::totalKwh(const std::vector<HourSlot>& plan) const {
    double total = 0.0;
    for (const auto& slot : plan) {
        total += slot.kwh;
    }
    return total;
}
