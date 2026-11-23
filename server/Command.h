//
// Created by arisu on 23/11/2026.
//

#ifndef KWDB_COMMAND_H
#define KWDB_COMMAND_H

#pragma once

enum class Command : uint8_t {
    CreateUser = 1,
    Login      = 2,
    Put        = 3,
    Get        = 4,
    Has        = 5,
    Remove     = 6,
    DeleteUser = 7,
    Invalid
};
#endif //KWDB_COMMAND_H