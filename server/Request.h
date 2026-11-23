//
// Created by arisu on 20/11/2026.
//

#ifndef KWDB_REQUEST_H
#define KWDB_REQUEST_H

#include "Command.h"
struct clientConnection;

struct Request {
    Command command;
    std::vector<std::string> args;
};

#endif //KWDB_REQUEST_H