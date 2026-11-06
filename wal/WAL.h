//
// Created by arisu on 06/11/2026.
//

#ifndef KWDB_WAL_H
#define KWDB_WAL_H
#include <filesystem>


class WAL {
    std::filesystem::path walPath;
    //immutable opcode (compile-time)
    static constexpr const char* PUT_OP = "PUT";
    static constexpr const char* REMOVE_OP = "RM";
    static constexpr const char* BEGIN = "BEGIN";
    static constexpr const char* COMMIT = "COMMIT";
    static constexpr const char* ROLLBACK = "ROLLBACK";

    static constexpr const char SEPARATOR = ':';

    public:
    WAL();

    void appendPut(
        const std::string& key,
        const std::string& value);

    void appendRemove(
        const std::string& key);
};


#endif //KWDB_WAL_H