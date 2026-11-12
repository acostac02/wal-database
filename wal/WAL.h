//
// Created by arisu on 06/11/2026.
//

#ifndef KWDB_WAL_H
#define KWDB_WAL_H
#include <filesystem>
#include <functional>
#include <optional>

class Database;

class WAL {

public:
    explicit WAL(
        std::filesystem::path path,
        uint64_t maxSegmentSize
        );

    struct Record {
        uint64_t lsn;
        uint8_t type;
        std::string key;
        std::string value;
    };

    static constexpr uint8_t PUT_OP = 0x01;
    static constexpr uint8_t REMOVE_OP = 0x02;

    uint64_t getLastLsn() const;
    uint64_t getMaxSegmentSize() const;
    uint64_t getTotalWalSize() const;

    void appendPut(
        const std::string &key,
        const std::string &value);

    void appendRemove(
        const std::string &key);

    void replay(
        uint64_t lastLsn,
        const std::function<void(const Record&)>& apply);

    void reset();

private:
    std::filesystem::path walFiesDir;
    uint64_t lastLsn_ = 0;
    uint64_t maxSegmentSize_ = 0;
    uint64_t currentSegmentId_ = 0;
    uint64_t currentSegmentSize_ = 0;
    uint64_t totalWalSize_ = 0;

    void writeRecord(const std::string &payload);

    void createWalFile(uint64_t segmentId);

    std::optional<uint64_t> discoverLastSegmentId();

    std::filesystem::path segmentPath(uint64_t segmentId) const;

    std::filesystem::path currentSegmentPath() const;

    void replaySegment(
        const std::filesystem::path& segment,
        uint64_t checkpointLsn,
        const std::function<void(const Record &)> &apply
        );

    std::vector<std::filesystem::path> discoverSegments() const;

    bool canFitRecord(uint64_t recordSize) const;

    void rotateSegment();
};

#endif //KWDB_WAL_H
