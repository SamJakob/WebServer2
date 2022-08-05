#pragma once
#include <tcp.h>

typedef enum {
    METHOD_UNKNOWN,
    GET,
    POST,
    PUT,
    PATCH,
    DELETE,
    OPTIONS
} HttpMethod;

static const char* http_method_names[] = {
        "UNKNOWN", "GET", "POST", "PUT", "PATCH", "DELETE", "OPTIONS"
};

typedef enum {
    READ_REQUEST_LINE,
    READ_HEADERS,
    READ_BODY,
    READ_DONE
} HttpServerRequestReadState;

typedef enum {
    VALID,
    MALFORMED_REQUEST,
    BAD_REQUEST_LINE,
    BAD_METHOD,
    BAD_VERSION
} HttpServerRequestStatus;

typedef struct {
    const char* accept;
    const char* connection;
    const char* contentLength;
    const char* host;
    const char* userAgent;
} HttpServerRequestStateHeaders;

typedef struct {
    HttpServerRequestStatus readStatus;
    HttpServerRequestReadState readState;
    int socket;

    // Request line
    HttpMethod method;
    const char* path;
    const char* httpVersion;

    // Headers
    const char* rawHeaders;
    int rawHeadersLength;
    const HttpServerRequestStateHeaders* headers;

    // Body
    const char* body;
    int bodyLength;
} HttpServerRequest;

HttpServerRequest* create_http_server_request();

void http_server_accept(TcpServer* server, HttpServerRequest* request);

void close_http_server_request(HttpServerRequest* request, const char* response);

void destroy_http_server_request(HttpServerRequest** request);

/**
 * Process an individual line of an HTTP response.
 * @param data The pointer to the overall payload.
 * @param length The length of the data, in bytes.
 * @param current A pointer to the current pointer into the payload.
 */
void http_server_accept_line(const char* data, unsigned long length, char** current, HttpServerRequest* request);

void janitor_http_server_request(HttpServerRequest* request);
