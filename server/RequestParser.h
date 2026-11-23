//
// Created by arisu on 23/11/2026.
//

#ifndef KWDB_REQUESTPARSER_H
#define KWDB_REQUESTPARSER_H

#include <string>
#include "Request.h"
#include "../common/format.h"

/**
* Helper function, push back two strings into the Request arg vector.
* Before the push-back, binary is interpreted calling the readString() function.
* The two arguments inserted are interpreted as key-value or userId-passwordHash.
*/
inline void pushBackStrings(Request &req, std::istringstream &in) {
    req.args.push_back(format::readString(in));
    req.args.push_back(format::readString(in));
}

/**
 * @brief parse a packet sent by a client.
 *
 * Reads the command and push payload arguments into the Request args field.
 * Command and args are read from the payload as bytes, then interpreted as strings.
 *
 * @param cmd requested command
 * @param payload payload received
 * @return the parsed request (as a Request struct)
 */
inline Request parse(uint8_t cmd, const std::string &payload) {
    Request req{};
    req.command = static_cast<Command>(cmd);

    std::istringstream in(payload, std::ios::binary);

    switch (req.command) {
        case Command::CreateUser:
            pushBackStrings(req, in); //credentials
            break;

        case Command::DeleteUser:
            break;

        case Command::Login:
            pushBackStrings(req, in); //credentials
            break;

        case Command::Put:
            pushBackStrings(req, in); //key-value
            break;

        case Command::Remove:
        case Command::Get:
        case Command::Has:
            req.args.push_back(format::readString(in)); //just the key
            break;

        default:
            break;
    }
    return req;
}


#endif //KWDB_REQUESTPARSER_H
