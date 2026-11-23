//
// Created by arisu on 20/11/2026.
//

#ifndef KWDB_SOCKETDEFS_H
#define KWDB_SOCKETDEFS_H

#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

using SocketHandle = SOCKET;
constexpr SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;

#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using SocketHandle = int;
constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;

#endif

inline void closeSocket(const SocketHandle socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

#endif //KWDB_SOCKETDEFS_H
