#include "elpris_api/elpris_api.h"

#include <http_utils.h>

int handle_elpris(HTTPServerConnection* conn, const char* query) {
    char* response_data = NULL;
    int   status        = 0;

    if (fetch_url_sync(
            "https://www.elprisetjustnu.se/api/v1/prices/2026/01-25_SE3.json",
            &response_data, &status) != 0) {
        return send_json_error(conn, 502, "Failed to fetch elpris data");
    }

    int rc = send_response(conn, 200, "application/json", response_data,
                           strlen(response_data));
    free(response_data);
    return rc;
}
