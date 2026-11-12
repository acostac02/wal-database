//
// Created by arisu on 06/11/2026.
//

#ifndef KWDB_DATABASE_H
#define KWDB_DATABASE_H
#include <filesystem>
#include <string>
#include <unordered_map>

#include "../wal/WAL.h"

class Database {
private:
    std::filesystem::path dbPath;
    std::unordered_map<std::string, std::string> data;
    WAL wal;
    uint64_t checkpointLsn = 0;
    const uint64_t checkpointThreshold = 0;

    void checkForCheckpoint();
    void checkpoint();

public:

    static constexpr uint64_t DEFAULT_SEGMENT_SIZE = 128;
    static constexpr uint64_t DEFAULT_THRESHOLD = 4 * 1024 * 1024;

    explicit Database(
        std::uint64_t chkpThreshold = DEFAULT_THRESHOLD,
        uint64_t walSegmentSize = DEFAULT_SEGMENT_SIZE
        );

    bool has(const std::string&) const;

    void put(
        const std::string& key,
        const std::string& value
        );

    std::string get(
        const std::string& key)
    const;

    void remove(const std::string& key);

    void saveToDisk();

    void loadFromDisk();

};


#endif //KWDB_DATABASE_H