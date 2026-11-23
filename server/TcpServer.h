//
// Created by arisu on 20/11/2026.
//

#ifndef KWDB_TCPSERVER_H
#define KWDB_TCPSERVER_H

#include "../storage/storage_engine/StorageEngine.h"
#include "Request.h"
#include "Response.h"
#include "Session.h"
#include "SocketDefs.h"
#include "RequestParser.h"
#include "../common/format.h"
#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

struct clientConnection {
    SocketHandle socket;
    sockaddr_in address;
};

class TcpServer {
public:
    explicit TcpServer(
        uint16_t port,
        uint64_t checkpointThreshold,
        uint64_t walSegmentSize
        );

    void run();

    void handleCLient(const clientConnection &cliConn);

    Request readRequest(const clientConnection &cliConn);

    Response processRequest(const Request &req, Session &session);

    void sendResponse(const clientConnection &cliConn, const Response &response);


private:
    uint8_t port_;
    SocketHandle listenSocket_;
    StorageEngine storage_;
};

#endif //KWDB_TCPSERVER_H
