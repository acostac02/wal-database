//
// Created by arisu on 06/11/2026.
//

#ifndef KWDB_WAL_H
#define KWDB_WAL_H
#include <filesystem>
#include <functional>

class Database;

class WAL {
private:
    std::filesystem::path walPath;
    uint64_t lastLsn_ = 0;

    void writeRecord(const std::string &payload);

public:
    struct Record {
        uint64_t lsn;
        uint8_t type;
        std::string key;
        std::string value;
    };

    static constexpr uint8_t PUT_OP = 0x01;
    static constexpr uint8_t REMOVE_OP = 0x02;

    explicit WAL(std::filesystem::path path);

    uint64_t getLastLsn() const;

    void appendPut(
        const std::string &key,
        const std::string &value);

    void appendRemove(
        const std::string &key);

    void replay(
        uint64_t lastLsn,
        const std::function<void(const Record&)>& apply);
};

#endif //KWDB_WAL_H
