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

static inline const char* seek_until_crlf(const char* buffer_ptr) {
    return strstr(buffer_ptr, "\r\n");
}

static inline const char* seek_after_crlf(const char* buffer_ptr) {
    return seek_until_crlf(buffer_ptr) + 2;
}

static inline const char* seek_until_space(const char* buffer_ptr) {
    return strchr(buffer_ptr, ' ');
}

static inline const char* seek_after_space(const char* buffer_ptr) {
    return seek_until_space(buffer_ptr) + 1;
}

void parse_http_header(const char* header, char* name, int name_buffer_size, char* value, int value_buffer_size) {
    bzero(name, name_buffer_size);
    bzero(value, value_buffer_size);

    unsigned long total_length = strlen(header);
    char* split = strchr(header, ':');

    unsigned long key_length = split - header;
    unsigned long value_length = total_length - ((uintptr_t) split + 1);

    strncpy(name, header, key_length > (name_buffer_size - 1) ? (name_buffer_size - 1) : key_length);
    strncpy(value, split + 1, value_length > (value_buffer_size - 1) ? (value_buffer_size - 1) : value_length);
}

void janitor_http_server_request(HttpServerRequest* request) {
    if (request->readState > READ_REQUEST_LINE) {
        free((void*) request->path);
        free((void*) request->httpVersion);
    }
    if (request->readState > READ_HEADERS) {
        free((void*) request->rawHeaders);
    }
}

#define END_WITH_STATUS(request, status) (request)->readState = READ_DONE; \
    (request)->readStatus = (status);                                      \
    return

#define CHECK_ADDR_OFFSET(current, offset, limit, cleanup) if ((uintptr_t) *(current) + (offset) >= (limit)) { cleanup; END_WITH_STATUS(request, MALFORMED_REQUEST); }
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
            uintptr_t ver_len = (uintptr_t) seek_until_crlf(*current) - (uintptr_t) *current;
            CHECK_ADDR_OFFSET(current, ver_len + 2, ptr_limit, {
                free(path);
            })
            char* ver = malloc(ver_len + 1);
            strncpy(ver, *current, ver_len);
            ((char*) ver)[ver_len] = '\0';
            request->httpVersion = ver;

            // Seek to headers
            *current += ver_len + 2;
            request->readState = READ_HEADERS;

            break;
        }

        case READ_HEADERS: {
            if (request->rawHeaders == NULL) {
                // Determine the number of headers that need to be saved.
                // Also determine the end of the headers.

                // Headers ends with double CRLF, so keep seeking until two consecutive
                // CRLFs are encountered.
                unsigned long headerCount = 0;
                unsigned long rawHeaderBytes = 0;
                const char* header_seek = *current;
                const char* header_seek_cache;

                do {
                    header_seek_cache = header_seek;
                    header_seek = seek_until_crlf(header_seek);

                    // Add difference for header seek (length of header).
                    rawHeaderBytes += (header_seek - header_seek_cache);
                    // Add one for null terminator byte.
                    rawHeaderBytes += 1;

                    header_seek += 2;
                    headerCount++;
                }
                    // Seek until the next CRLF is immediately after the current one.
                while (seek_until_crlf(header_seek) != header_seek);

                request->rawHeadersBytesRead = 0;
                request->rawHeadersCountRead = 0;
                request->rawHeadersCount = headerCount;
                request->rawHeadersLength = rawHeaderBytes;
                request->rawHeadersBase = malloc(rawHeaderBytes);
                request->rawHeaders = calloc(headerCount, sizeof(const char*));
            }

            // Read and save the current header.
            if (request->rawHeadersBytesRead < request->rawHeadersLength) {
                // Determine the length in bytes of the current header.
                char* currentHeader = (char*) seek_until_crlf(*current);
                uintptr_t header_length = currentHeader - *current;

                // Copy the current header into the buffer reserved for all the request headers.
                strncpy(request->rawHeadersBase + request->rawHeadersBytesRead, *current, header_length);
                // Mark the base address of the header string in rawHeaders.
                request->rawHeaders[request->rawHeadersCountRead] = request->rawHeadersBase + request->rawHeadersBytesRead;

                // Update the metrics on the number of headers read.
                request->rawHeadersBytesRead += header_length + 1;
                request->rawHeadersCountRead++;

                // Seek past the current header (and its CRLF).
                *current = currentHeader + 2;
            }
            // If we've read all the headers, continue to the READ_BODY state.
            else request->readState = READ_BODY;

            break;
        }

        case READ_BODY: {
            request->body = NULL;
            request->bodyLength = 0;

            request->readState = READ_DONE;

            break;
        }

        case READ_DONE: { return; }
    }
}
