C server providing an energy plan for households with solar panels and battery

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

## Development

1. Clone the repository with submodules:
```bash
git clone --recurse-submodules https://github.com/stockholm-3/just-api.git
cd just-api
```

3. Run the daemon(this starts the server and handles fetching for the energy plan):
```bash
make daemon-start
```
you can stop the daemon with:
```bash
make daemon-stop
```

## API Docs

see [api_docs.md](docs/api/api_docs.md) for API documentation

## Deployment

see https://github.com/Stockholm-3/just-api-deployment


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
