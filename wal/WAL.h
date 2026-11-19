//
// Created by arisu on 06/11/2026.
//

#ifndef KWDB_WAL_H
#define KWDB_WAL_H
#include <filesystem>
#include <functional>
#include <optional>
#include <fstream>
#include "../user_db/UserDatabase.h"

class Database;

enum class RecordType : uint8_t {
    PutKV      = 0x01,
    RemoveKV   = 0x02,
    CreateUser = 0x03,
    DeleteUser = 0x04
};

struct Record {
    uint64_t lsn;
    uint64_t ownerId;
    RecordType type;
    std::string key;
    std::string value;
};

class WAL {
public:

    explicit WAL(
        std::filesystem::path path,
        uint64_t maxSegmentSize
    );

    ~WAL();

    uint64_t getLastLsn() const;

    uint64_t getMaxSegmentSize() const;

    uint64_t getTotalWalSize() const;

    void appendPut(
        uint64_t ownerId,
        const std::string &key,
        const std::string &value
    );

    void appendRemove(
        uint64_t ownerId,
        const std::string &key
    );

    void appendCreateUser(
        uint64_t ownerId,
        const std::string &userName,
        const std::string &passwordHash);

    void appendDeleteUser(uint64_t userId);

    void replay(
        uint64_t lastLsn,
        const std::function<void(const Record &)> &apply);

    void reset();

private:
    std::filesystem::path walFilesDir;
    uint64_t lastLsn_ = 0;
    uint64_t maxSegmentSize_ = 0;
    uint64_t currentSegmentId_ = 0;
    uint64_t currentSegmentSize_ = 0;
    uint64_t totalWalSize_ = 0;
    std::ofstream currentActiveSegment_;

    void writeRecord(const std::string &payload);

    void createWalFile(uint64_t segmentId, uint64_t firstLsn);

    std::optional<uint64_t> discoverLastSegmentId();

    std::filesystem::path segmentPath(uint64_t segmentId) const;

    std::filesystem::path currentSegmentPath() const;

    void replaySegment(
        const std::filesystem::path &segment,
        uint64_t checkpointLsn,
        const std::function<void(const Record &)> &apply,
        bool isLast
    );

    std::vector<std::filesystem::path> discoverSegments() const;

    bool canFitRecord(uint64_t recordSize) const;

    void rotateSegment();

    void refreshSizes();
};

#endif //KWDB_WAL_H
