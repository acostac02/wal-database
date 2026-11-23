//
// Created by arisu on 20/11/2026.
//

#include "TcpServer.h"
#include "Session.h"

#include <chrono>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#define poll WSAPoll
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace {
    constexpr auto MAX_IDLE = std::chrono::minutes(20); //Client disconnection for inactivity

    /**
     * @brief Reads an exact number of bytes from a socket.
     *
     * Repeatedly calls recv() until the requested number of bytes has been received. This function is
     * required because a single recv() call may return fewer bytes than requested even when additional
     * data is available.
     *
     * @param sock Source socket.
     * @param buffer Destination buffer.
     * @param size Number of bytes to read.
     *
     * @throws std::runtime_error If the connection is closed or a receive error occurs before all bytes
     *                            are read.
     */
    void readExact(
        const SocketHandle sock,
        char *buffer,
        const size_t size
    ) {
        size_t total = 0;
        while (total < size) {
            int received = recv(
                sock,
                buffer + total,
                static_cast<int>(size - total),
                0
            );

            if (received <= 0) {
                throw std::runtime_error("connection closed");
            }
            total += received;
        }
    }

    /**
     * @brief Sends an exact number of bytes through a socket.
     *
     * Repeatedly calls send() until the entire buffer has been transmitted. This function is required
     * because a single send() call may transfer only part of the provided data.
     *
     * @param sock Destination socket.
     * @param data Buffer containing the bytes to send.
     * @param size Number of bytes to transmit.
     *
     * @throws std::runtime_error If the connection is closed or a transmission error occurs before all
     *                            bytes are sent.
     */
    void sendExact(
        const SocketHandle sock,
        const char *data,
        const size_t size
    ) {
        size_t total = 0;

        while (total < size) {
            int sent = send(
                sock,
                data + total,
                static_cast<int>(size - total),
                0
            );

            if (sent == SOCKET_ERROR) {
                throw std::runtime_error("Failed to send data");
            }

            total += sent;
        }
    }
}

/**
 * @brief Creates and initializes the TCP server.
 *
 * Initializes the StorageEngine and all platform-specific networking resources.
 * On Windows, the Winsock subsystem is initialized before creating the listening socket.
 *
 * The constructor creates the server socket, configures socket options, binds it to the
 * specified port and starts listening for incoming client connections.
 *
 * @param port TCP port used to accept incoming connections.
 * @param checkpointThreshold WAL size threshold that triggers a checkpoint.
 * @param walSegmentSize Maximum size, in bytes, of a single WAL segment.
 *
 * @throws std::runtime_error If any networking initialization step fails.
 */
TcpServer::TcpServer(
    const uint16_t port,
    const uint64_t checkpointThreshold,
    const uint64_t walSegmentSize
)
    : storage_(checkpointThreshold, walSegmentSize) {
#ifdef _WIN32
    WSADATA wsaData{};

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
#endif

    listenSocket_ = socket(
        AF_INET,
        SOCK_STREAM,
        IPPROTO_TCP
    );

    if (listenSocket_ == INVALID_SOCKET_HANDLE) {
        throw std::runtime_error("socket creation failed");
    }

    int enable = 1;

#ifdef _WIN32
    setsockopt(
        listenSocket_,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<char *>(&enable),
        sizeof(enable)
    );

#else
    setsockopt(listenSocket_,
               SOL_SOCKET,
               SO_REUSEADDR,
               &enable,
               sizeof(enable
               );
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET; //address family internet (socket use ipv4)
    addr.sin_port = htons(port); //set the port number
    addr.sin_addr.s_addr = htonl(INADDR_ANY); //listen on all network interfaces

    if (bind(listenSocket_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        closeSocket(listenSocket_);
        throw std::runtime_error("bind failed");
    }

    if (listen(listenSocket_, SOMAXCONN) < 0) {
        closeSocket(listenSocket_);
        throw std::runtime_error("listen failed");
    }

    port_ = port;
}

/**
 * @brief Reads a complete request from the client.
 *
 * The function uses readExact() to guarantee that all fields of the request packet are received
 * before parsing.
 *
 * The request protocol is defined as:
 * - 1 byte  : command
 * - 4 bytes : payload size
 * - N bytes : payload
 *
 * After the packet is reconstructed, the payload is forwarded to the request parser and converted
 * into a Request object.
 *
 * @param cliConn Client connection information, including socket and address.
 * @return Parsed Request object.
 *
 * @throws std::runtime_error If the connection is closed or a
 *         network error occurs.
 */
Request TcpServer::readRequest(const clientConnection &cliConn) {
    //read first 8 bytes to identify the command
    uint8_t command;
    readExact(cliConn.socket, reinterpret_cast<char *>(&command), sizeof(command));

    //read 4 bytes for the payload size
    uint32_t payloadSize;
    readExact(cliConn.socket, reinterpret_cast<char *>(&payloadSize), sizeof(payloadSize));

    //initialize the payload string with all 0 and read the content
    std::string payload(payloadSize, '\0');
    readExact(cliConn.socket, payload.data(), payloadSize);

    return parse(command, payload);
}

/**
 * @brief Process the user request.
 *
 * Switch the request command and pass the arguments to the correct storage engine function.
 * Retrieve the user id from the session, check and build the response.
 *
 * @param req the user request (command and arguments).
 * @param session the user session, to retrieve the id.
 * @return the built response.
 */
Response TcpServer::processRequest(const Request &req, Session &session) {
    Response resp{};
    try {
        switch (req.command) {
            case Command::CreateUser: {
                const auto userId = storage_.createUser(req.args[0], req.args[1]);
                resp.success = true;
                resp.payload = userId; //remember when the user receive the response that is uint64
                break;
            }

            case Command::Login: {
                auto id = storage_.authenticate(req.args[0], req.args[1]);
                if (!id) {
                    resp.success = false;
                    resp.payload = "Authentication failed";
                    break;
                }
                session.authenticated = true;
                session.userId = *id;

                resp.success = true;
                resp.payload = "Login successful";

                break;
            }

            case Command::Put: {
                if (!session.authenticated) {
                    resp.success = false;
                    resp.payload = "Not authenticated";
                }

                storage_.put(session.userId, req.args[0], req.args[1]);

                resp.success = true;
                break;
            }

            case Command::Get: {
                if (!session.authenticated) {
                    resp.success = false;
                    resp.payload = "Not authenticated";
                }

                auto value = storage_.get(session.userId, req.args[0]);

                if (!value) {
                    resp.success = false;
                    resp.payload = "Key not found";
                    break;
                }

                resp.success = true;
                resp.payload = *value;
                break;
            }

            case Command::Has: {
                if (!session.authenticated) {
                    resp.success = false;
                    resp.payload = "Not authenticated";
                }

                if (bool has = storage_.has(session.userId, req.args[0]); !has) {
                    resp.success = false;
                    resp.payload = "Key not found";
                    break;
                }

                resp.success = true;
                resp.payload = "true";
                break;
            }

            case Command::DeleteUser: {
                if (!session.authenticated) {
                    resp.success = false;
                    resp.payload = "Not authenticated";
                    break;
                }

                session.authenticated = false;
                session.userId = 0;

                resp.success = true;
                resp.payload = "Success";
                break;
            }

            case Command::Remove: {
                if (!session.authenticated) {
                    resp.success = false;
                    resp.payload = "Not authenticated";
                    break;
                }

                if (bool removed = storage_.remove(session.userId, req.args[0]); !removed) {
                    resp.success = false;
                    resp.payload = "Key not found";
                    break;
                }

                resp.success = true;
                resp.payload = "Removed";
                break;
            }

            case Command::Invalid: {
                resp.success = false;
                resp.payload = "Invalid command";
                break;
            }

            default: {
                resp.success = false;
                resp.payload = "Unknown command";
                break;
            }
        }
    } catch (const std::exception &e) {
        resp.success = false;
        resp.payload = e.what();
    }
    return resp;
}

/**
 * @brief Sends a response packet to the connected client.
 *
 * Serializes the Response object into its binary protocol representation and transmits it
 * through the client's socket using sendExact().
 *
 * The response protocol is defined as:
 * - 1 byte  : success flag (0 = failure, 1 = success)
 * - 4 bytes : payload size
 * - N bytes : payload
 *
 * @param cliConn Client connection information, including socket and address.
 * @param response Response to serialize and send.
 *
 * @throws std::runtime_error If the response cannot be transmitted.
 */
void TcpServer::sendResponse(const clientConnection &cliConn, const Response &response) {
    std::ostringstream payload(std::ios::binary);

    format::writeString(payload, response.payload);
    const std::string payloadBytes = payload.str();

    uint8_t success = response.success ? 1 : 0;
    uint32_t payloadSize = static_cast<uint32_t>(payloadBytes.size());

    sendExact(cliConn.socket, reinterpret_cast<char *>(&success), sizeof(success));
    sendExact(cliConn.socket, reinterpret_cast<char *>(&payloadSize), sizeof(payloadSize));
    sendExact(cliConn.socket, payloadBytes.data(), payloadBytes.size());
}

/**
 * @brief Client handling function.
 *
 * Initialize the session, manage the request / response and close the connection.
 *
 * @param cliConn client connection, include the socket and address
 */
void TcpServer::handleCLient(const clientConnection &cliConn) {
    Session session{};

    auto lastActivity = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now - lastActivity > MAX_IDLE) {
            break;
        }

        Request req = readRequest(cliConn);
        lastActivity = std::chrono::steady_clock::now();

        if (req.command == Command::Invalid) {
            break;
        }

        Response resp = processRequest(req, session);
        sendResponse(cliConn, resp);
    }

    closeSocket(cliConn.socket);
}

/**
 * @brief Server loop function.
 *
 * Check and accept the client connection request creating a clientConnection structure that
 * contains socket and address, then delegate the client handling to handleClient() function.
 */
void TcpServer::run() {
    while (true) {
        sockaddr_in addr{};

#ifdef _WIN32
        int addrLen = sizeof(addr);
#else
        socklen_t addrLen = sizeof(addr);
#endif

        SocketHandle client = accept(
            listenSocket_,
            reinterpret_cast<sockaddr *>(&addr),
            &addrLen
        );

        if (client == INVALID_SOCKET_HANDLE) {
            std::cerr << "accept failed" << std::endl;
            continue;
        }

        const clientConnection cliConn{
            client,
            addr
        };

        handleCLient(cliConn);
    }
}
