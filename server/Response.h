//
// Created by arisu on 20/11/2026.
//

#ifndef KWDB_RESPONSE_H
#define KWDB_RESPONSE_H

#pragma once

#include <string>

struct Response {
    bool success;
    std::string payload;
};

#endif //KWDB_RESPONSE_H