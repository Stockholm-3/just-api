C server providing an energy plan for housholds with solar panels and battery

>Just Weather is a **HTTP server** built as a school project at **Chas Academy (SUVX25)** by **Team Stockholm 3**.  
>It acts as a bridge between clients and [open-meteo.com](https://open-meteo.com), providing real-time weather data via a simple REST API

![C](https://img.shields.io/badge/C-%2300599C.svg?style=flat&logo=c&logoColor=white)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
---

## Related repos
[stockholm-3-lib](https://github.com/Stockholm-3/lib) - Library used for all Stockholm-3 projects. Includes external libraries like Jansson for compiling from source.

## Features

- **24 hour enegy plan** - to optimise energy usage
- **Live weather data** — temperature, weather conditions, and wind speed

---

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

The project uses a symlink to access our library and Jansson. The symlink should point to:
```bash
lib/ -> ../lib/
```
More symmlinks may be added in the future.

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

All responses follow a standardized format with `success` field and `data` payload.

---

### Endpoints

#### 1. Get Current Weather by Coordinates

```
GET /v1/current?lat={latitude}&lon={longitude}
```

**Description:**
Retrieves current weather data for specific geographic coordinates.

**Query Parameters:**

| Parameter | Type   | Required | Description                        | Example  |
|-----------|--------|----------|------------------------------------|----------|
| `lat`     | float  | Yes      | Latitude of the location           | `59.33`  |
| `lon`     | float  | Yes      | Longitude of the location          | `18.07`  |

**Example Request:**
```bash
curl "http://stockholm3.onvo.se/v1/current?lat=59.33&lon=18.07"
```

**Response Format:**
```json
{
  "success": true,
  "data": {
    "current_weather": {
      "temperature": 27.0,
      "temperature_unit": "°C",
      "windspeed": 17.8,
      "windspeed_unit": "km/h",
      "wind_direction_10m": 173,
      "wind_direction_name": "South",
      "weather_code": 2,
      "weather_description": "Partly cloudy",
      "is_day": 1,
      "precipitation": 0.0,
      "precipitation_unit": "mm",
      "humidity": 0.0,
      "pressure": 1008.6,
      "time": "2025-12-07T20:15"
    },
    "location": {
      "latitude": 1.0,
      "longitude": 2.0
    }
  }
}
```

**Response Fields:**

| Field                                    | Type    | Description                                      |
|------------------------------------------|---------|--------------------------------------------------|
| `success`                                | boolean | Whether the request was successful               |
| `data.location.latitude`                 | float   | Latitude of the requested location               |
| `data.location.longitude`                | float   | Longitude of the requested location              |
| `data.current_weather.temperature`       | float   | Current air temperature                          |
| `data.current_weather.temperature_unit`  | string  | Temperature unit (°C)                            |
| `data.current_weather.windspeed`         | float   | Wind speed                                       |
| `data.current_weather.windspeed_unit`    | string  | Wind speed unit (km/h)                           |
| `data.current_weather.wind_direction_10m`  | integer | Wind direction in degrees (0-360)              |
| `data.current_weather.wind_direction_name` | string  | Cardinal direction (e.g., "South", "North-East") |
| `data.current_weather.weather_code`      | integer | Weather condition code (WMO standard)            |
| `data.current_weather.weather_description` | string  | Human-readable weather description             |
| `data.current_weather.is_day`            | integer | 1 if daytime, 0 if nighttime                     |
| `data.current_weather.precipitation`     | float   | Precipitation amount                             |
| `data.current_weather.precipitation_unit`  | string  | Precipitation unit (mm)                        |
| `data.current_weather.humidity`          | float   | Relative humidity percentage                     |
| `data.current_weather.pressure`          | float   | Atmospheric pressure in hPa                      |
| `data.current_weather.time`              | string  | Timestamp in ISO 8601 format (YYYY-MM-DDTHH:MM)  |

---

#### 2. Get Weather by City Name

```
GET /v1/weather?city={city_name}&country={country_code}
```

**Description:**
Retrieves current weather for a city by name. Automatically geocodes the city and returns weather data with location details.

**Query Parameters:**

| Parameter | Type   | Required | Description                              | Example     |
|-----------|--------|----------|------------------------------------------|-------------|
| `city`    | string | Yes      | City name (URL encoded)                  | `Stockholm` |
| `country` | string | No       | ISO 3166-1 alpha-2 country code          | `SE`        |
| `region`  | string | No       | Region/state name for disambiguation     | `California`|

**Example Requests:**
```bash
# Simple city search
curl "http://stockholm3.onvo.se/v1/weather?city=Stockholm"

# City with country filter
curl "http://stockholm3.onvo.se/v1/weather?city=Stockholm&country=SE"

# City with region (for disambiguation)
curl "http://stockholm3.onvo.se/v1/weather?city=Paris&region=Texas&country=US"
```

**Response Format:**
```json
{
  "success": true,
  "data": {
    "location": {
      "name": "Stockholm",
      "country": "Sweden",
      "country_code": "SE",
      "region": "Stockholm",
      "latitude": 59.329380035400391,
      "longitude": 18.068710327148438,
      "population": 1515017,
      "timezone": "Europe/Stockholm"
    },
    "current_weather": {
      "temperature": 6.2,
      "temperature_unit": "°C",
      "weather_code": 3,
      "weather_description": "Overcast",
      "windspeed": 6.8,
      "windspeed_unit": "km/h",
      "wind_direction_10m": 153,
      "wind_direction_name": "South-Southeast",
      "humidity": 0.0,
      "pressure": 1006.6,
      "precipitation": 0.0,
      "is_day": 0
    }
  }
}
```

**Response Fields:**

| Field                                | Type    | Description                                |
|--------------------------------------|---------|--------------------------------------------|
| `success`                            | boolean | Request success status                     |
| `data.location.name`                 | string  | City name                                  |
| `data.location.country`              | string  | Full country name                          |
| `data.location.country_code`         | string  | ISO 3166-1 alpha-2 country code            |
| `data.location.region`               | string  | Region/state/province name                 |
| `data.location.latitude`             | float   | Latitude coordinate                        |
| `data.location.longitude`            | float   | Longitude coordinate                       |
| `data.location.population`           | integer | City population                            |
| `data.location.timezone`             | string  | IANA timezone identifier                   |
| `data.current_weather.temperature`   | float   | Current temperature                        |
| `data.current_weather.temperature_unit` | string | Temperature unit (°C)                   |
| `data.current_weather.weather_code`  | integer | WMO weather code                           |
| `data.current_weather.weather_description` | string | Human-readable weather description  |
| `data.current_weather.windspeed`     | float   | Wind speed                                 |
| `data.current_weather.windspeed_unit`| string  | Wind speed unit (km/h)                     |
| `data.current_weather.wind_direction_10m` | integer | Wind direction in degrees (0-360)     |
| `data.current_weather.wind_direction_name` | string | Cardinal wind direction            |
| `data.current_weather.humidity`      | float   | Relative humidity percentage               |
| `data.current_weather.pressure`      | float   | Atmospheric pressure (hPa)                 |
| `data.current_weather.precipitation` | float   | Precipitation amount (mm)                  |
| `data.current_weather.is_day`        | integer | 1 if daytime, 0 if nighttime               |

---

#### 3. City Search (Autocomplete)

```
GET /v1/cities?query={search_term}
```

**Description:**
Search for cities by name prefix. Optimized for autocomplete with 3-tier search strategy:
1. Popular Cities DB (in-memory, fastest)
2. File cache (fast)
3. Open-Meteo Geocoding API (fallback)

**Query Parameters:**

| Parameter | Type   | Required | Description                              | Example  |
|-----------|--------|----------|------------------------------------------|----------|
| `query`   | string | Yes      | City search term (min 2 characters)      | `Stock`  |

**Example Requests:**
```bash
# Autocomplete search
curl "http://stockholm3.onvo.se/v1/cities?query=Stock"

# Single city lookup
curl "http://stockholm3.onvo.se/v1/cities?query=Kyiv"
```

**Response Format:**
```json
{
  "success": true,
  "data": {
    "query": "Stock",
    "count": 9,
    "cities": [
      {
        "name": "Stockholm",
        "country": "Sweden",
        "country_code": "SE",
        "latitude": 59.327499389648438,
        "longitude": 18.05470085144043,
        "population": 995574
      },
      {
        "name": "Stockton",
        "country": "United States",
        "country_code": "US",
        "latitude": 37.97650146484375,
        "longitude": -121.31089782714844,
        "population": 416005
      }
    ]
  }
}
```

**Response Fields:**

| Field                     | Type    | Description                                |
|---------------------------|---------|--------------------------------------------|
| `success`                 | boolean | Request success status                     |
| `data.query`              | string  | The search query used                      |
| `data.count`              | integer | Number of cities found                     |
| `data.cities`             | array   | Array of city objects                      |
| `cities[].name`           | string  | City name                                  |
| `cities[].country`        | string  | Full country name                          |
| `cities[].country_code`   | string  | ISO 3166-1 alpha-2 country code            |
| `cities[].latitude`       | float   | Latitude coordinate                        |
| `cities[].longitude`      | float   | Longitude coordinate                       |
| `cities[].population`     | integer | City population (optional)                 |
| `cities[].region`         | string  | Region/state/province (optional)           |

---

### Error Responses

All endpoints return standardized error responses:

```json
{
  "success": false,
  "error": {
    "code": 400,
    "type": "Bad Request",
    "message": "Missing required parameter: query"
  }
}
```

**Common Error Codes:**

| Code | Type                  | Description                                |
|------|-----------------------|--------------------------------------------|
| 400  | Bad Request           | Missing or invalid parameters              |
| 404  | Not Found             | Endpoint or resource not found             |
| 500  | Internal Server Error | Server-side error or API failure           |

---

### CORS Support

All endpoints support CORS with `Access-Control-Allow-Origin: *` header for frontend integration.
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
