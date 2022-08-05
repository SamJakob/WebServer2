#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool listening;
    uint16_t port;

    int socket;
} TcpServer;

TcpServer* create_tcp_server(uint16_t port);
void destroy_tcp_server(TcpServer** server);

bool start_tcp_server(TcpServer* server);
void stop_tcp_server(TcpServer* server);
