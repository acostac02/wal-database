//
// Created by arisu on 20/11/2026.
//

#ifndef KWDB_SESSION_H
#define KWDB_SESSION_H
#include <cstdint>

struct Session {
    bool authenticated;
    uint64_t userId;
};

#endif //KWDB_SESSION_H