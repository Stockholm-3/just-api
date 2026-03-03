#include "compute.h"
#include "logger.h"

#define LOG_MODULE "ALGO"

void run_energy_algorithm(const AlgoInput* input, AlgoOutput* output,
                          bool decisions[SLOTS_PER_DAY]) {
    double buy_count        = 0.0;
    double direct_use_count = 0.0;
    double charge_count     = 0.0;
    double sell_count       = 0.0;

    /* number of slots we care about; zero means use full day */
    int slots = (input->slots > 0 && input->slots <= SLOTS_PER_DAY)
                    ? input->slots
                    : SLOTS_PER_DAY;

    /* Calculate average electricity price */
    double sum = 0.0;
    for (int i = 0; i < slots; i++) {
        sum += input->elpris[i];
    }
    double avg_price = (slots > 0) ? sum / slots : 0.0;
    LOG_DEBUG(LOG_MODULE, "Algo: avg price = %.5f SEK/kWh", avg_price);

    /* Pre‑compute per‑slot decision (buy vs sell) if caller requested it */
    if (decisions) {
        for (int i = 0; i < slots; i++) {
            decisions[i] = (input->elpris[i] < avg_price);
        }
        /* fill remaining slots with false for safety */
        for (int i = slots; i < SLOTS_PER_DAY; i++) {
            decisions[i] = false;
        }
    }

    /* Simple algorithm: if price low, buy/charge and if high, use/sell */
    for (int i = 0; i < slots; i++) {
        if (input->elpris[i] < avg_price) {
            /* Price is low: buy or charge battery */
            if (input->sun_intensity[i] > 0.5) {
                charge_count += 1.0;
            } else {
                buy_count += 1.0;
            }
        } else {
            /* Price is high: use directly or sell excess */
            if (input->temperature[i] > 0.5) {
                direct_use_count += 1.0;
            } else {
                sell_count += 1.0;
            }
        }
    }

    /* Normalize to percentages */
    output->buy_electricity = (buy_count / slots) * 100.0;
    output->direct_use      = (direct_use_count / slots) * 100.0;
    output->charge_battery  = (charge_count / slots) * 100.0;
    output->sell_excess     = (sell_count / slots) * 100.0;
}