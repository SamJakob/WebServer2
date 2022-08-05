#include <stdio.h>

#define ALLOW_PORT_REUSE 1

#include <tcp.h>
#include <http.h>
#include <sys/signal.h>
#include <stdlib.h>

void handle_SIGTERM(int signal_no);

static TcpServer* server;

int main() {
    // Handle signals
    signal(SIGTERM, handle_SIGTERM);

    // Create and start HTTP server.
    server = create_tcp_server(8080);
    if (start_tcp_server(server)) {
        printf(
            "Successfully started HTTP server on port %u.\n",
            server->port
        );
    } else {
        printf("Failed to start HTTP server.\n");
    }

    // Process requests in a loop whilst the server is listening.
    while (server->listening) {
        HttpServerRequest request = {0};

        // Accept request.
        http_server_accept(server, &request);
        printf("Got %s request to %s\n", http_method_names[request.method], request.path);

        // Close request with hardcoded response.
        close_http_server_request(&request, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\nHello, world!");
    }

    // Destroy and clean up HTTP server.
    destroy_tcp_server(&server);
    return 0;
}

void handle_SIGTERM(int signal_no) {
    printf("SIGTERM(%d): Shutting down web server...\n", signal_no);
    destroy_tcp_server(&server);
    exit(143);
}
