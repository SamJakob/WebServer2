#include <http.h>

#include <netinet/in.h>
#include <strings.h>
#include <printf.h>
#include <unistd.h>
#include <stdlib.h>

HttpServerRequest* create_http_server_request() {
    HttpServerRequest* request = malloc(sizeof(HttpServerRequest));
    bzero(request, sizeof(HttpServerRequest));
    return request;
}

void http_server_accept(TcpServer* server, HttpServerRequest* request) {
    int client = accept(server->socket, (struct sockaddr*) NULL, NULL);
    if (!server->listening) return;

    char buffer[1024];
    bzero(buffer, 1024);

    unsigned long buffer_length;

    bool continueReading = true;

    // Read the request data kb by kb until \r\n is reached at the end of a chunk.
    // (This indicates the end of the headers).
    // TODO: make this not scuffed
    do {
        recv(client, buffer, 1024, 0);
        buffer_length = strlen(buffer);

        if (buffer[buffer_length - 2] == '\r' &&
            buffer[buffer_length - 1] == '\n')
            continueReading = false;
    } while (continueReading);

    char* current = buffer;

    request->readState = READ_REQUEST_LINE;
    while (request->readState != READ_DONE) {
        http_server_accept_line(buffer, buffer_length, &current, request);
    }

    request->socket = client;
}

void close_http_server_request(HttpServerRequest* request, const char* response) {
    if (request->socket == 0) return;

    if (response != NULL) {
        write(request->socket, response, strlen(response));
    }

    close(request->socket);
    request->socket = 0;
    janitor_http_server_request(request);
}

void destroy_http_server_request(HttpServerRequest** request) {
    close_http_server_request(*request, NULL);
    *request = NULL;
}

/**
 * Seek a string buffer until the next CRLF is reached.
 * The pointer after that CRLF is returned.
 *
 * @param buffer_ptr The pointer into the buffer to start reading.
 * @return The pointer after the next CRLF.
 */
const char* seek_until_crlf(const char* buffer_ptr) {
    // Keep seeking until \r is found, followed by \n.
    while (*(++buffer_ptr) != '\r' && *(++buffer_ptr) != '\n');
    // Then, return the seeked buffer pointer, having seeked to after the \r\n.
    return buffer_ptr;
}

const char* seek_after_crlf(const char* buffer_ptr) {
    return seek_until_crlf(buffer_ptr) + 2;
}

const char* seek_until_space(const char* buffer_ptr) {
    // Keep seeking until space is found.
    while (*(++buffer_ptr) != ' ');
    return buffer_ptr;
}

const char* seek_after_space(const char* buffer_ptr) {
    return seek_until_space(buffer_ptr) + 1;
}

void janitor_http_server_request(HttpServerRequest* request) {
    if (request->readState > READ_REQUEST_LINE) {
        free((void*) request->path);
        free((void*) request->httpVersion);
    }
}

#define END_WITH_STATUS(request, status) (request)->readState = READ_DONE; \
    (request)->readStatus = (status);                                      \
    return

#define CHECK_ADDR_OFFSET(current, offset, limit, cleanup) if ((uintptr_t) *(current) + (offset) >= (limit)) { END_WITH_STATUS(request, MALFORMED_REQUEST); cleanup }
#define CHECK_ADDR(current, limit, cleanup) CHECK_ADDR_OFFSET(current, 0, limit, cleanup)

void http_server_accept_line(const char* data, unsigned long length, char** current, HttpServerRequest* request) {
    uintptr_t ptr_limit = (uintptr_t) (data + length);

    switch (request->readState) {
        case READ_REQUEST_LINE: {
            uintptr_t method_len = (uintptr_t) seek_until_space(*current) - (uintptr_t) *current;
            CHECK_ADDR_OFFSET(current, method_len + 1, ptr_limit, {})

            // Request Method
            request->method = METHOD_UNKNOWN;
            if (method_len == 3) {
                if (strncmp(*current, http_method_names[GET], method_len) == 0) request->method = GET;
                else if (strncmp(*current, http_method_names[PUT], method_len) == 0) request->method = PUT;
            } else if (method_len == 4) {
                if (strncmp(*current, http_method_names[POST], method_len) == 0) request->method = POST;
            } else if (method_len == 5) {
                if (strncmp(*current, http_method_names[PATCH], method_len) == 0) request->method = PATCH;
            } else if (method_len == 6) {
                if (strncmp(*current, http_method_names[DELETE], method_len) == 0) request->method = DELETE;
            } else if (method_len == 7) {
                if (strncmp(*current, http_method_names[OPTIONS], method_len) == 0) request->method = OPTIONS;
            }
            if (request->method == METHOD_UNKNOWN) {
                END_WITH_STATUS(request, BAD_METHOD);
            }

            *current = *current + method_len + 1;

            // Request Path
            uintptr_t path_len = (uintptr_t) seek_until_space(*current) - (uintptr_t) *current;
            CHECK_ADDR_OFFSET(current, path_len + 1, ptr_limit, {})
            char* path = malloc(path_len + 1);
            strncpy(path, *current, path_len);
            path[path_len] = '\0';
            request->path = path;
            *current = *current + path_len + 1;

            // Request HTTP version
            uintptr_t ver_len = (uintptr_t) seek_until_space(*current) - (uintptr_t) *current;
            CHECK_ADDR_OFFSET(current, path_len + 1, ptr_limit, {
                free(path);
            })
            const char* ver = malloc(ver_len + 1);
            ((char*) ver)[ver_len] = '\0';
            request->httpVersion = ver;

            // Seek to headers
            *current = (char*) seek_after_crlf(*current);
            request->readState = READ_HEADERS;
        }

        case READ_HEADERS: {
            // Determine the number of headers that need to be saved.
            // Also determine the end of the headers.
            // TODO

            // Save the headers line by line.
            // TODO

            request->readState = READ_BODY;
        }

        case READ_BODY: {
            request->body = NULL;
            request->bodyLength = 0;

            request->readState = READ_DONE;
        }

        case READ_DONE: { return; }
    }
}
