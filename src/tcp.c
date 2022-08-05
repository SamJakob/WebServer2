#include <tcp.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>

TcpServer* create_tcp_server(uint16_t port) {
    TcpServer* server = malloc(sizeof(TcpServer));
    server->port = port;
    return server;
}

void destroy_tcp_server(TcpServer** server) {
    if (*server == NULL) return;

    if ((*server)->listening) stop_tcp_server(*server);
    free(*server);
    *server = NULL;
}

bool start_tcp_server(TcpServer* server) {
    if (server->listening) return false;

    // Declare server socket address.
    struct sockaddr_in serverAddress;
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(server->port);

    // Initialize server socket and set socket options
    server->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->socket == -1) return false;

#if HTTP_APP_DEBUG
    // If we're in debug mode, allow TCP port reuse.
    int soReusePort = 1;
    setsockopt(server->socket, SOL_SOCKET, SO_REUSEPORT, &soReusePort, sizeof(soReusePort));
    printf("Security Warning: SO_REUSEPORT = 1");
#endif

    // Now start listening for connections
    if (bind(
        server->socket,
        (struct sockaddr*) &serverAddress,
        sizeof(serverAddress)
    ) == -1) return false;

    listen(server->socket, 10 /* backlog */);

    server->listening = true;
    return true;
}

void stop_tcp_server(TcpServer* server) {
    shutdown(server->socket, SHUT_RDWR);
    close(server->socket);
    server->listening = false;
}
