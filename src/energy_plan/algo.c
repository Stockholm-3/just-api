#include "algo.h"

#include "logger/logger.h"

#include <string.h>

#define LOG_MOD "ALGO"

void algo_run(const AlgoInput* input, AlgoOutput* output) {
    memset(output, 0, sizeof(*output));

    int slots = (input->slots > 0 && input->slots <= SLOTS_PER_DAY)
                    ? input->slots
                    : SLOTS_PER_DAY;

    // Average price over valid slots — used as the decision threshold.
    double sum = 0.0;
    for (int i = 0; i < slots; i++) {
        sum += input->elpris[i];
    }
    double avg_price = slots > 0 ? sum / slots : 0.0;

    LOG_DEBUG(LOG_MOD, "avg price = %.5f SEK/kWh over %d slots", avg_price,
              slots);

    /*
     * Per-slot fractions.
     *
     * The strength of the signal drives the weight: the further the slot
     * price is from the average, the more confidently we commit to one
     * action pair.  Within each price regime the split between the two
     * sub-actions is driven by sun_intensity (low price) or temperature
     * (high price).
     *
     * All four fractions sum to exactly 1.0 per slot.
     */
    for (int i = 0; i < slots; i++) {
        double price_delta = input->elpris[i] - avg_price;

        /*
         * price_weight: how strongly does this slot deviate from average?
         * Clamped to [0, 1] against a ±50 % band around the average so
         * that a 50 %+ deviation gives full confidence.
         */
        double band         = (avg_price > 0.0) ? avg_price * 0.5 : 1.0;
        double price_weight = price_delta / band;
        if (price_weight > 1.0) {
            price_weight = 1.0;
        }
        if (price_weight < -1.0) {
            price_weight = -1.0;
        }

        AlgoSlot* s = &output->slots[i];

        if (price_delta <= 0.0) {
            /*
             * Low-price slot: split between buy and charge.
             * sun_intensity drives the charge fraction.
             */
            double low_confidence  = -price_weight;           /* 0..1 */
            double charge_fraction = input->sun_intensity[i]; /* 0..1 */

            double active = low_confidence; /* total weight on this regime */

            s->charge_battery  = active * charge_fraction;
            s->buy_electricity = active * (1.0 - charge_fraction);
            s->direct_use      = 0.0;
            s->sell_excess     = 1.0 - active; /* remainder → neutral/sell */
        } else {
            /*
             * High-price slot: split between direct_use and sell.
             * temperature drives the direct_use fraction.
             */
            double high_confidence = price_weight; /* 0..1 */
            double use_fraction    = input->temperature[i] > 0.0
                                         ? input->temperature[i] / 40.0
                                         : 0.0;
            if (use_fraction > 1.0) {
                use_fraction = 1.0;
            }

            double active = high_confidence;

            s->direct_use      = active * use_fraction;
            s->sell_excess     = active * (1.0 - use_fraction);
            s->buy_electricity = 1.0 - active; /* remainder → neutral/buy */
            s->charge_battery  = 0.0;
        }
    }

    /* Summary: mean fraction across all valid slots. */
    double acc_buy = 0.0, acc_use = 0.0, acc_charge = 0.0, acc_sell = 0.0;
    for (int i = 0; i < slots; i++) {
        acc_buy += output->slots[i].buy_electricity;
        acc_use += output->slots[i].direct_use;
        acc_charge += output->slots[i].charge_battery;
        acc_sell += output->slots[i].sell_excess;
    }
    double dslots                   = (double)slots;
    output->summary.buy_electricity = acc_buy / dslots;
    output->summary.direct_use      = acc_use / dslots;
    output->summary.charge_battery  = acc_charge / dslots;
    output->summary.sell_excess     = acc_sell / dslots;
}
