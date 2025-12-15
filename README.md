![Just Weather logo](https://i.imgur.com/m6CMJxz.png)

A lightweight C weather server providing a simple HTTP REST API

>Just Weather is a **HTTP server** built as a school project at **Chas Academy (SUVX25)** by **Team Stockholm 3**.  
>It acts as a bridge between clients and [open-meteo.com](https://open-meteo.com), providing real-time weather data via a simple REST API

![C](https://img.shields.io/badge/C-%2300599C.svg?style=flat&logo=c&logoColor=white)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Build](https://github.com/Stockholm-3/just-weather/actions/workflows/build.yml/badge.svg)](https://github.com/Stockholm-3/just-weather/actions/workflows/build.yml)
[![Check Formatting](https://github.com/Stockholm-3/just-weather/actions/workflows/format-check.yml/badge.svg)](https://github.com/Stockholm-3/just-weather/actions/workflows/format-check.yml)
---

## Features

- **Live weather data** — temperature, weather conditions, and wind speed
- **Open-Meteo integration** — no API key required
- **C99-compatible** — portable and minimal dependencies

---

## Requirements

- Linux / WSL environment
- GCC (C99 compliant)
- **jansson** (included as submodule or symlink)
- `make`

## Installation

1. Clone the repository:
```bash
git clone https://github.com/stockholm-3/just-weather-server.git
cd just-weather
```

2. Ensure the lib branch is cloned into ../lib:

To clone the jansson library from project root run:
```bash
make install-lib
```
This will create a lib folder outside of the root with all library source files.

The project uses a symlink to access the Jansson library. The symlink should point to:
```bash
lib/jansson -> ../../lib/jansson
```
More symmlinks may be added in the future.

If the symlink is broken or missing, recreate it from the project root:
```bash
rm lib/jansson
ln -s ../../lib/jansson lib/
```

3. Run the server:
```bash
make run-server
```

Binaries will be created in:
```bash
build/<mode>/just-weather-server
```

## Weather API Documentation

**Base URL:**
```
http://stockholm3.onvo.se/v1/
```

---

### Endpoints

```
GET /current
```

**Description:**  
Retrieves the current weather data for the specified geographic coordinates.

**Query Parameters:**  

| Parameter | Type   | Required | Description                        |
|-----------|--------|----------|------------------------------------|
| `lat`     | float  | Yes      | Latitude of the location (e.g., 59.33) |
| `lon`     | float  | Yes      | Longitude of the location (e.g., 18.07) |

**Example Request:**  
```bash
curl "http://stockholm3.onvo.se:81/v1/current?lat=59.33&lon=18.07"
```

**Response Fields:**

| Field                         | Type    | Description                                      |
|-------------------------------|---------|--------------------------------------------------|
| `coords.latitude`             | float   | Latitude of the requested location               |
| `coords.longitude`            | float   | Longitude of the requested location              |
| `current.temperature`         | float   | Current air temperature                          |
| `current.temperature_unit`    | string  | Temperature unit used (e.g. °C)                  |
| `current.windspeed`           | float   | Wind speed                                       |
| `current.windspeed_unit`      | string  | Wind speed unit (e.g. km/h)                      |
| `current.wind_direction_10m`  | integer | Wind direction in degrees                        |
| `current.wind_direction_name` | string  | Cardinal direction of the wind (e.g. South)      |
| `current.weather_code`        | integer | Weather condition code                           |
| `current.weather_description` | string  | Human-readable weather description               |
| `current.is_day`              | integer | 1 if daytime, 0 if nighttime                     |
| `current.precipitation`       | float   | Precipitation amount                             |
| `current.precipitation_unit`  | string  | Precipitation measurement unit (e.g. mm)         |
| `current.humidity`            | float   | Relative humidity percentage                     |
| `current.pressure`            | float   | Atmospheric pressure in hPa                      |
| `current.time`                | integer | UNIX timestamp of the measurement                |
| `current.city_name`           | string  | Name of the location                             |

## Example Response
```json
{
  "current": {
    "temperature": 27.0,
    "temperature_unit": "°C",
    "windspeed": 17.8,
    "windspeed_unit": "km/h",
    "wind_direction_10m": 173,
    "weather_code": 2,
    "is_day": 1,
    "precipitation": 0.0,
    "precipitation_unit": "mm",
    "humidity": 0.0,
    "pressure": 1008.6,
    "time": 1764084412,
    "city_name": "Location (1.0000, 2.0000)",
    "weather_description": "Partly cloudy",
    "wind_direction_name": "South"
  },
  "coords": {
    "latitude": 1.0,
    "longitude": 2.0
  }
}
```

## Authors

**Team Stockholm 3**
- Chas Academy, SUVX25
- 2025-11-04

## License

This project is licensed under the MIT License - see the [License](LICENSE) file for details.

## Acknowledgments

- [Open-Meteo API](https://open-meteo.com/) - Free weather API
- [Jansson](https://github.com/akheron/jansson) - JSON parsing library
- Chas Academy instructor and classmates
