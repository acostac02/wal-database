//
// Created by arisu on 06/11/2026.
//

#ifndef KWDB_DATABASE_H
#define KWDB_DATABASE_H
#include <filesystem>
#include <string>
#include <unordered_map>
#include <shared_mutex>

#include "../wal/WAL.h"

struct Header {
    uint32_t version = 2;
    uint64_t checkpoint_lsn = 0;
    uint64_t count = 0;
};

/// Key of the pair key-value, contains the owner id.
struct Key {
    uint64_t ownerId;
    std::string key;

    bool operator==(const Key &other) const {
        return ownerId == other.ownerId && key == other.key;
    }
};

/// Combine the two hashes of owner id and key.
/// The constant distributes all the value of h2 to all bits, the shift mix h1 and h2 by themselves.
struct KeyHash {
    std::size_t operator()(const Key &key) const {
        const std::size_t h1 = std::hash<uint64_t>{}(key.ownerId);
        const std::size_t h2 = std::hash<std::string>{}(key.key);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

class Database {
private:
    std::filesystem::path dbPath;
    std::unordered_map<Key, std::string, KeyHash> data;

    WAL wal;
    mutable std::shared_mutex dbMutex_;

    uint64_t checkpointLsn = 0;
    const uint64_t checkpointThreshold = 0;

    void checkForCheckpoint();

    void checkpoint();

public:
    static constexpr uint64_t DEFAULT_SEGMENT_SIZE = 16 * 1024 * 1024;
    static constexpr uint64_t DEFAULT_THRESHOLD = 64 * 1024 * 1024;

    explicit Database(
        std::uint64_t chkpThreshold = DEFAULT_THRESHOLD,
        uint64_t walSegmentSize = DEFAULT_SEGMENT_SIZE
    );

    void put(
        uint64_t ownerId,
        const std::string &key,
        const std::string &value
    );

    std::optional<std::string> get(
        uint64_t ownerId,
        const std::string &key)
    const;

    bool has(uint64_t ownerId, const std::string &key) const;

    void remove(uint64_t ownerId, const std::string &key);

    void saveToDisk();

    void loadFromDisk();
};


#endif //KWDB_DATABASE_H
