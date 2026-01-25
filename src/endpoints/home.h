#include <http_utils.h>
#include <string.h>

int handle_homepage(HTTPServerConnection* conn, const char* query) {
    const char* html = "<!DOCTYPE html>"
                       "<html>"
                       "<head><title>Just Weather</title></head>"
                       "<body>"
                       "<h1>Just Weather API</h1>"
                       "<p>Available endpoints:</p>"
                       "<ul>"
                       "  <li><b>GET /echo</b> - echo raw request</li>"
                       "  <li><b>POST /echo</b> - echo raw body</li>"
                       "  <li><b>GET /v1/current?lat=XX&lon=YY</b> - current "
                       "weather by coordinates</li>"
                       "  <li><b>GET /v1/weather?city=NAME&country=CODE</b> - "
                       "weather by city name</li>"
                       "  <li><b>GET /v1/cities?query=SEARCH</b> - city search "
                       "(autocomplete)</li>"
                       "</ul>"
                       "<p>Source code on <a "
                       "href=\"https://github.com/Stockholm-3/"
                       "just-weather-server\" target=\"_blank\">GitHub</a>.</p>"
                       "</body>"
                       "</html>";

    return send_response(conn, 200, "text/html", html, strlen(html));
}
