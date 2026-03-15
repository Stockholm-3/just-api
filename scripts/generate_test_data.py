#!/usr/bin/env python3
"""Generate test fixture data for the compute binary."""

import json
import math
import os
import time

BASE_DIR = "energy_plan"
INPUT_DIR = os.path.join(BASE_DIR, "compute_input")
OUTPUT_DIR = os.path.join(BASE_DIR, "compute_output")
CITIES_CSV = os.path.join(BASE_DIR, "cities.csv")

CITIES = [
    ("Stockholm",  "SE3", 59.329380, 18.068710),
    ("Gothenburg", "SE3", 57.706700, 11.966700),
    ("Malmo",      "SE4", 55.605100, 13.003800),
    ("Sundsvall",  "SE2", 62.390800, 17.306900),
    ("Lulea",      "SE1", 65.584500, 22.154900),
    ("Uppsala",    "SE3", 59.858600, 17.638700),
]

SLOTS_PER_DAY = 96
DATE = "2026-03-15"
TZ_OFFSET = "+01:00"

# Base prices per zone (SEK/kWh), realistic SE1 < SE2 < SE3 < SE4
ZONE_BASE_PRICE = {"SE1": 0.40, "SE2": 0.55, "SE3": 0.75, "SE4": 0.90}


def slot_time(slot: int) -> str:
    h = (slot * 15) // 60
    m = (slot * 15) % 60
    return f"{DATE}T{h:02d}:{m:02d}:00{TZ_OFFSET}"


def price_for_slot(zone: str, slot: int) -> float:
    """Realistic price: peak 07-09 and 17-20, cheap at night."""
    base = ZONE_BASE_PRICE.get(zone, 0.75)
    hour = (slot * 15) / 60
    # morning peak
    if 7 <= hour < 9:
        factor = 1.8
    # evening peak
    elif 17 <= hour < 20:
        factor = 1.6
    # night cheap
    elif hour < 6 or hour >= 22:
        factor = 0.6
    else:
        factor = 1.0
    noise = 0.02 * math.sin(slot * 0.3)
    return round(base * factor + noise, 4)


def sun_intensity(slot: int, lat: float) -> float:
    """Simple sun model: rises ~07:00, peaks at noon, sets ~17:00."""
    hour = (slot * 15) / 60
    sunrise = 7.0
    sunset = 17.0
    if hour < sunrise or hour > sunset:
        return 0.0
    peak = (sunrise + sunset) / 2
    angle = math.pi * (hour - sunrise) / (sunset - sunrise)
    intensity = math.sin(angle) * 800  # W/m²
    # northern cities get less sun
    lat_factor = 1.0 - max(0, (lat - 55) / 30) * 0.3
    return round(max(0.0, intensity * lat_factor), 2)


def temperature(slot: int, lat: float) -> float:
    """Diurnal temperature: min at ~06:00, max at ~14:00."""
    hour = (slot * 15) / 60
    # colder further north
    base_temp = 10.0 - (lat - 55) * 0.4
    amplitude = 6.0
    phase = 2 * math.pi * (hour - 6) / 24
    return round(base_temp + amplitude * math.sin(phase), 2)


def generate_elpris() -> None:
    zones = ["SE1", "SE2", "SE3", "SE4"]
    entries = []
    for zone in zones:
        for slot in range(SLOTS_PER_DAY):
            entries.append({
                "time_start": slot_time(slot),
                "price_zone": zone,
                "SEK_per_kWh": price_for_slot(zone, slot),
            })
    path = os.path.join(INPUT_DIR, "elpris_merged.json")
    with open(path, "w") as f:
        json.dump(entries, f, indent=2)
    print(f"  wrote {path} ({len(entries)} entries)")


def generate_weather(city: str, lat: float, lon: float) -> None:
    forecast = []
    for slot in range(SLOTS_PER_DAY):
        forecast.append({
            "temperature": temperature(slot, lat),
            "sun_intensity": sun_intensity(slot, lat),
        })
    data = {"data": {"minutely_forecast": forecast}}
    name = f"{city.lower()}-{lat:.6f}-{lon:.6f}.json"
    path = os.path.join(INPUT_DIR, name)
    with open(path, "w") as f:
        json.dump(data, f, indent=2)
    print(f"  wrote {path}")


def generate_cities_csv() -> None:
    ts = int(time.time())
    lines = []
    for city, zone, lat, lon in CITIES:
        lines.append(f"{city},{zone},{lat:.6f},{lon:.6f},{ts}")
    with open(CITIES_CSV, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"  wrote {CITIES_CSV} ({len(lines)} cities)")


def main() -> None:
    os.makedirs(INPUT_DIR, exist_ok=True)
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    print("Generating cities.csv...")
    generate_cities_csv()
    print("Generating elpris_merged.json...")
    generate_elpris()
    print("Generating weather files...")
    for city, _, lat, lon in CITIES:
        generate_weather(city, lat, lon)
    print("\nDone. Run: ./build/debug/compute")


if __name__ == "__main__":
    main()
