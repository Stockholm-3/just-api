# Just API — Reference Documentation

**Base URL:** `To be defined`  
**Protocol:** HTTP/1.1  
**Format:** All responses are JSON unless noted otherwise.  
**Source:** [github.com/Stockholm-3/just-api](https://github.com/Stockholm-3/just-api)

---

## Overview

Just Api is a lightweight HTTP API that aggregates weather data via Open-Meteo, Swedish electricity spot prices via Elpriset Just Nu, and an energy planning layer that combines both. It is designed to be self-hosted and dependency-light.

All endpoints respond synchronously from cache where possible. On a cache miss, requests are dispatched asynchronously to upstream APIs. Cache TTLs are documented per endpoint.

---

## Response Format

All JSON responses follow a consistent envelope:

**Success**
```json
{
  "status": "ok",
  "data": { ... }
}
```

**Error**
```json
{
  "message": "Human-readable error description"
}
```

HTTP status codes follow standard conventions: `200 OK`, `202 Accepted`, `400 Bad Request`, `404 Not Found`, `500 Internal Server Error`, `503 Service Unavailable`.

---

## Endpoints

### Utility

---

#### `GET /health`

Returns server status. Use this to verify the process is alive and accepting connections.

**Parameters:** None

**Response — 200 OK**
```json
{"status": "ok"}
```

---

#### `GET /echo`

Echoes the raw request back to the client as plain text. Useful for inspecting headers and query strings.

**Parameters:** None

**Response — 200 OK**
```
Content-Type: text/plain
<raw request bytes>
```

#### `POST /echo`

Echoes the raw request body back to the client.

**Body:** Any content.

**Response — 200 OK**
```
Content-Type: text/plain
<raw body bytes>
```

---

### Weather

All weather data is sourced from [Open-Meteo](https://open-meteo.com/). Weather endpoints support two input modes: **coordinates mode** (lat/lon) and **city mode** (geocoded via the Open-Meteo Geocoding API). The two modes are mutually exclusive; if `city` is present, city mode is used.

---

#### `GET /v1/current`

Returns current weather conditions for a given coordinate pair.

**Cache TTL:** 15 minutes

**Parameters**

| Name | Required | Type | Description |
|------|----------|------|-------------|
| `lat` | Yes | float | Latitude in decimal degrees |
| `lon` | Yes | float | Longitude in decimal degrees |

**Example Request**
```
GET /v1/current?lat=59.3293&lon=18.0686
```

**Response — 200 OK**
```json
{
  "status": "ok",
  "data": {
    "current_weather": {
      "temperature": 4.2,
      "temperature_unit": "°C",
      "windspeed": 6.1,
      "windspeed_unit": "km/h",
      "wind_direction_10m": 215,
      "wind_direction_name": "SW",
      "weather_code": 3,
      "weather_description": "Overcast",
      "is_day": 1,
      "precipitation": 0.0,
      "precipitation_unit": "mm",
      "humidity": 78.0,
      "pressure": 1012.4,
      "time": "2025-03-04T14:00"
    },
    "location": {
      "latitude": 59.3293,
      "longitude": 18.0686
    }
  }
}
```

---

#### `GET /v1/weather`

Returns current weather conditions by city name. The city name is resolved via geocoding before fetching weather data.

**Cache TTL:** 15 minutes (weather); 7 days (geocoding)

**Parameters**

| Name | Required | Type | Description |
|------|----------|------|-------------|
| `city` | Yes | string | City name |
| `country` | No | string | ISO 3166-1 alpha-2 country code (e.g. `SE`) |
| `region` | No | string | Region or state name to disambiguate results |

**Example Request**
```
GET /v1/weather?city=Stockholm&country=SE
```

**Response — 200 OK**

Same structure as `/v1/current`.

---

#### `GET /v1/hourly`

Returns an hourly weather forecast.

**Cache TTL:** 1 hour

**Coordinates Mode Parameters**

| Name | Required | Type | Description |
|------|----------|------|-------------|
| `lat` | Yes | float | Latitude |
| `lon` | Yes | float | Longitude |
| `hours` | No | integer | Forecast window in hours. Default: `24`. Range: `1`–`168`. |

**City Mode Parameters**

| Name | Required | Type | Description |
|------|----------|------|-------------|
| `city` | Yes | string | City name |
| `country` | No | string | ISO country code |
| `region` | No | string | Region to narrow geocoding results |
| `hours` | No | integer | Forecast window in hours. Default: `24`. Range: `1`–`168`. |

**Example Request**
```
GET /v1/hourly?lat=59.3293&lon=18.0686&hours=24
```

**Response — 200 OK**
```json
{
  "status": "ok",
  "data": {
    "location": {
      "latitude": 59.3293,
      "longitude": 18.0686
    },
    "hourly_forecast": [
      {
        "time": "2025-03-04T14:00",
        "temperature": 4.2,
        "temperature_unit": "°C",
        "humidity": 78.0,
        "precipitation": 0.0,
        "weather_code": 3,
        "weather_description": "Overcast",
        "windspeed": 6.1,
        "windspeed_unit": "km/h",
        "wind_direction": 215,
        "wind_direction_name": "SW",
        "pressure": 1012.4,
        "is_day": 1
      }
    ]
  }
}
```

Aliases: `GET /v1/forecast/hourly` maps to the same handler.

---

#### `GET /v1/minutely`

Returns a 15-minute interval weather forecast. Includes direct solar radiation and a normalised sun intensity value in addition to standard weather fields.

**Cache TTL:** 15 minutes

**Coordinates Mode Parameters**

| Name | Required | Type | Description |
|------|----------|------|-------------|
| `lat` | Yes | float | Latitude |
| `lon` | Yes | float | Longitude |
| `hours` | No | integer | Forecast window in hours. Default: `24`. Range: `1`–`168`. Each hour produces 4 steps (every 15 minutes). |

**City Mode Parameters**

| Name | Required | Type | Description |
|------|----------|------|-------------|
| `city` | Yes | string | City name |
| `country` | No | string | ISO country code |
| `region` | No | string | Region to narrow geocoding results |
| `hours` | No | integer | Forecast window in hours |

**Example Request**
```
GET /v1/minutely?lat=59.3293&lon=18.0686&hours=6
```

**Response — 200 OK**
```json
{
  "status": "ok",
  "data": {
    "location": {
      "latitude": 59.3293,
      "longitude": 18.0686
    },
    "minutely_forecast": [
      {
        "time": "2025-03-04T14:00",
        "temperature": 4.2,
        "temperature_unit": "°C",
        "humidity": 78.0,
        "precipitation": 0.0,
        "weather_code": 3,
        "weather_description": "Overcast",
        "windspeed": 6.1,
        "windspeed_unit": "km/h",
        "wind_direction": 215,
        "wind_direction_name": "SW",
        "pressure": 1012.4,
        "is_day": 1,
        "direct_radiation_wm2": 142.5,
        "sun_intensity": 0.143
      }
    ]
  }
}
```

`sun_intensity` is `direct_radiation_wm2 / 1000`, clamped to `[0, 1]`. A value of `1.0` corresponds to peak solar irradiance at sea level (1000 W/m²).

Aliases: `GET /v1/forecast/minutely` maps to the same handler.

---

#### `GET /v1/forecast`

General forecast endpoint. Accepts the same parameters as `/v1/hourly` and returns hourly forecast data.

**Example Request**
```
GET /v1/forecast?lat=59.3293&lon=18.0686&hours=48
```

---

#### `GET /v1/cities`

City autocomplete and search. Uses a three-tier strategy: local popular-cities database, geocoding cache, then the Open-Meteo Geocoding API. Results from the API are not written to cache.

**Parameters**

| Name | Required | Type | Description |
|------|----------|------|-------------|
| `query` | Yes | string | Partial or full city name. Minimum 2 characters. |

**Example Request**
```
GET /v1/cities?query=Stock
```

**Response — 200 OK**
```json
{
  "status": "ok",
  "data": {
    "results": [
      {
        "id": 2673730,
        "name": "Stockholm",
        "latitude": 59.332,
        "longitude": 18.0649,
        "country": "Sweden",
        "country_code": "SE",
        "admin1": "Stockholm County",
        "admin2": "",
        "population": 972647,
        "timezone": "Europe/Stockholm"
      }
    ]
  }
}
```

---

### Electricity

---

#### `GET /v1/elpris`

Returns Swedish electricity spot prices from [Elpriset Just Nu](https://www.elprisetjustnu.se/). Prices are fetched per price zone per day.

**Cache TTL:** Until 13:00 Swedish time the following day for current/tomorrow's prices; 10 years for historical dates.

**Parameters**

| Name | Required | Type | Description |
|------|----------|------|-------------|
| `price` | Yes | string | Price zone. One of: `SE1`, `SE2`, `SE3`, `SE4`. |
| `date` | No | string | Date in `YYYY-MM-DD` format. Omit to receive the latest available prices. After 13:00 Swedish time, "latest" refers to tomorrow's prices. |

**Price Zones**

| Zone | Region |
|------|--------|
| SE1 | Northern Sweden (Luleå) |
| SE2 | Northern-central Sweden (Sundsvall) |
| SE3 | Southern-central Sweden (Stockholm) |
| SE4 | Southern Sweden (Malmö) |

**Example Requests**
```
GET /v1/elpris?price=SE3
GET /v1/elpris?price=SE1&date=2025-01-15
```

**Response — 200 OK**

Raw JSON array from Elpriset Just Nu:
```json
[
  {
    "SEK_per_kWh": 0.5423,
    "EUR_per_kWh": 0.0487,
    "EXR": 11.1299,
    "time_start": "2025-03-04T00:00:00+01:00",
    "time_end": "2025-03-04T01:00:00+01:00"
  }
]
```

**Response — 503 Service Unavailable**

Returned when the upstream Elpriset Just Nu service is unreachable or the requested date has no data.

---

#### `GET /v1/get_plan`

Registers a Swedish city in the energy plan store and returns a computed energy plan that combines weather and electricity price data. On the first request for a city, the plan is queued for computation and a `202 Accepted` is returned. Subsequent requests return the computed plan.

City coordinates are resolved from a bundled CSV of Swedish city locations (`data/swedish_cities_locations.csv`). The city name is normalised to title case before lookup.

**Parameters**

| Name | Required | Type | Description |
|------|----------|------|-------------|
| `city` | Yes | string | Swedish city name. Case-insensitive. |
| `price` | Yes | string | Price zone. One of: `SE1`, `SE2`, `SE3`, `SE4`. |

**Example Request**
```
GET /v1/get_plan?city=Stockholm&price=SE3
```

**Response — 200 OK**

```json
{
  "status": "ok",
  "data": { ... }
}
```

**Response — 202 Accepted**

```json
{
  "message": "City 'Stockholm' has just been registered. Data will be available within 1 hour."
}
```

Returned when the city is new or the plan has not yet been computed.

**Response — 400 Bad Request**

```json
{"message": "City not found: Atlantis"}
```

Returned when the city name does not exist in the bundled Swedish city database, or when `price` is not a valid zone.

**Response — 503 Service Unavailable**

Returned when an internal read lock cannot be acquired. The request should be retried.

---

## Error Reference

| Status | Meaning |
|--------|---------|
| 400 | Missing or invalid query parameters |
| 404 | Endpoint not found |
| 500 | Internal server error (allocation failure, I/O error) |
| 503 | Upstream service unavailable or internal lock contention |

---

## Caching

| Endpoint | TTL |
|----------|-----|
| `/v1/current` | 15 minutes |
| `/v1/weather` | 15 minutes (weather) / 7 days (geocoding) |
| `/v1/hourly` | 1 hour |
| `/v1/minutely` | 15 minutes |
| `/v1/elpris` (current/tomorrow) | Until 13:00 Swedish time next day |
| `/v1/elpris` (historical) | 10 years |
| `/v1/cities` (geocoding) | 7 days (read-only; autocomplete does not write to cache) |

Cache is file-based and stored under `./cache/`. Each endpoint maintains its own cache directory with keyed entries derived from request parameters.

---

## Notes

- All times returned by weather endpoints are formatted as `YYYY-MM-DDTHH:MM` in the server's local timezone unless stated otherwise.
- Electricity prices from `/v1/elpris` use the timezone-aware ISO 8601 format supplied by the upstream API.
- The `/v1/get_plan` endpoint only supports Swedish cities present in the bundled coordinate dataset.
- Wind direction names use standard 16-point compass notation (N, NNE, NE, ..., NNW).
