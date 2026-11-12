//
// Created by arisu on 06/11/2026.
//

#include "wal.h"
#include "../format.h"
#include "../fileoperations/fileio.h"

#include <chrono>
#include <filesystem>
#include "fstream"
#include <cstring>
#include <iostream>
#include <optional>

using namespace format;

namespace {
    constexpr uint32_t MAX_RECORD = 64 * 1024 * 1024; //64MB
    const char MAGIC[4] = {'K', 'V', 'W', 'L'};
    constexpr uint32_t WAL_VERSION = 1;
    constexpr uint64_t HEADER_SIZE = sizeof(MAGIC) + sizeof(WAL_VERSION);
    const std::string PREFIX = "wal_";
    const std::string SUFFIX = ".log";

    void writeHeader(std::ofstream &out) {
        out.write(MAGIC, sizeof(MAGIC));
        writeValue(out, WAL_VERSION);
    }

    void readWalHeader(std::istream &walFile) {
        char magic[4];
        walFile.read(magic, sizeof(magic));
        if (!walFile || memcmp(magic, MAGIC, 4) != 0) {
            throw std::runtime_error("Incorrect magic in wal.");
        }

        const auto version = readValue<uint32_t>(walFile);
        if (version != WAL_VERSION) {
            throw std::runtime_error("wal file version not supported: " + std::to_string(version));
        }
    }

    /// Returns the current offset by using tellg() and throws an exception if the function returns -1.
    std::streamoff currentOffset(std::istream &in) {
        const std::streampos pos = in.tellg();
        if (pos == std::streampos(-1))
            throw std::runtime_error("tellg() failed in WAL replay.");
        return pos;
    }

    /// Extract the segment id from the wal file name.
    uint64_t extractSegmentId(const std::filesystem::path &filePath) {
        std::string filename = filePath.filename().string();
        if (!filename.starts_with(PREFIX) || !filename.ends_with(SUFFIX)) {
            throw std::runtime_error("Segment id extraction failed: error in the file name.");
        }

        auto underscorePos = filename.find("_");
        auto dotPos = filename.rfind(".");

        std::string serial = filename.substr(
            underscorePos + 1,
            dotPos - underscorePos - 1
        );

        return std::stoull(serial);
    }
}

/// Wal class constructor, if the wal files directory does not exist is created and the
/// first segment file is inserted (file + header). The file is created even if the dir
/// exist but is empty.
/// @param path the path where the directory is created.
WAL::WAL(
    std::filesystem::path path,
    uint64_t maxSegmentSize
)
    : walFiesDir(std::move(path)),
      maxSegmentSize_(maxSegmentSize) {
    std::filesystem::create_directories(walFiesDir);

    auto last = discoverLastSegmentId();
    if (!last) {
        createWalFile(0);
        last = 0;
    }
    currentSegmentId_ = *last;
    currentSegmentSize_ = std::filesystem::file_size(currentSegmentPath());

    totalWalSize_ = 0;
    for (auto const& segment : discoverSegments()) {
        totalWalSize_ += std::filesystem::file_size(segment);
    }
}

/// Iterates through wal files, returns the last segment id.
std::optional<uint64_t> WAL::discoverLastSegmentId() {
    std::optional<uint64_t> lastId;

    for (auto const dirEntry: std::filesystem::directory_iterator(walFiesDir)) {
        if (!dirEntry.is_regular_file()) {
            continue;
        }

        try {
            uint64_t id = extractSegmentId(dirEntry.path());
            lastId = lastId ? std::max(*lastId, id) : id;
        } catch (const std::exception &) {
            continue;
        }
    }
    return lastId;
}

/// Build a wal segment path with the id passed as argument.
std::filesystem::path WAL::segmentPath(uint64_t segmentId) const {
    std::ostringstream oss;

    oss << PREFIX
            << std::setw(6)
            << std::setfill('0')
            << segmentId
            << SUFFIX;

    return walFiesDir / oss.str();
}

/// Build and return the current wal segment path.
std::filesystem::path WAL::currentSegmentPath() const {
    return segmentPath(currentSegmentId_);
}

/// Verify if the new record can fit in the current wal file.
bool WAL::canFitRecord(uint64_t recordSize) const {
    return currentSegmentSize_ + recordSize <= maxSegmentSize_;
}

/// Creates a WAL segment file.
/// Segment names use a zero-padded serial number:
/// wal_000000.log, wal_000001.log, wal_000002.log, ...
/// After creation, the WAL header is written, flushed,
/// and forced to stable storage using fsync().
void WAL::createWalFile(uint64_t segmentId) {
    auto walPath = segmentPath(segmentId);

    std::ofstream file(walPath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot create wal segment.");
    }
    writeHeader(file);

    file.flush();
    if (!file) {
        throw std::runtime_error("Cannot write WAL header.");
    }

    file.close();
    syncFile(walPath);
    totalWalSize_ += HEADER_SIZE;
}

void WAL::rotateSegment() {
    createWalFile(currentSegmentId_ + 1);
    ++currentSegmentId_;
    currentSegmentSize_ = HEADER_SIZE;
}

uint64_t WAL::getMaxSegmentSize() const {
    return maxSegmentSize_;
}

uint64_t WAL::getTotalWalSize() const {
    return totalWalSize_;
}


/// Appends one record to the WAL file, in append mode.
/// Record layout: [payload size: uint32][CRC-32 of payload: uint32][payload].
/// The record is assembled in memory and written with a single write call.
/// If the record can't fit in the actual wal file, the rotation is called before the
/// insertion.
/// @param payload Serialized operation (lsn, op code, key, value).
/// @throws std::runtime_error if the file cannot be opened or written.
void WAL::writeRecord(const std::string &payload) {
    const uint64_t recordSize = sizeof(uint32_t) * 2 + payload.size();

    if (currentSegmentSize_ > HEADER_SIZE && !canFitRecord(recordSize)) {
        rotateSegment();
    }

    std::ofstream out(currentSegmentPath(), std::ios::binary | std::ios::app);

    if (!out.is_open()) {
        throw std::runtime_error("Cannot open WAL file for append.");
    }

    std::ostringstream rec(std::ios::binary);
    writeValue(rec, static_cast<uint32_t>(payload.size()));
    writeValue(rec, crc32(payload));
    rec.write(payload.data(), static_cast<std::streamsize>(payload.size()));

    const std::string bytes = rec.str();
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) {
        throw std::runtime_error("Cannot write WAL record.");
    }

    out.close();
    if (out.fail()) {
        throw std::runtime_error("Cannot close wal file after the record write.");
    }

    syncFile(currentSegmentPath());
    totalWalSize_ += bytes.size();
    currentSegmentSize_ += bytes.size();
}

/// Returns the LSN of the last record written to the WAL (or restored by
/// replay()). The next record will use lastLsn() + 1.
uint64_t WAL::getLastLsn() const {
    return lastLsn_;
}

/// Opens the wal file for the replay.
/// After checking the header (magic and version), read the file records and if the lsn is greater than the checkpoint
/// lsn the record is inserted into the unsorted map by using the apply function passed as argument (older record are
/// discarded).
/// The cycle is interrupted if the recalculated checksum does not match the extracted one, or if the size (len) exceeds
/// the maximum value MAX_RECORD and then if the vald offset < file size, the file is truncated at the last valid record.
/// @param checkpointLsn: the last checkpoint for confront.
/// @param apply: function to the unsorted map insertion, passed by the caller.
/// @throws std::runtime_error if the wal file cannot be opened or if an unknown op code is read.
void WAL::replaySegment(
    const std::filesystem::path &segment,
    uint64_t checkpointLsn,
    const std::function<void(const Record &)> &apply
) {
    std::ifstream walFile(segment, std::ios::binary);
    if (!walFile.is_open()) {
        throw std::runtime_error(
            "Cannot open WAL segment for replay."
        );
    }

    readWalHeader(walFile);
    std::streamoff validOffset = currentOffset(walFile);

    while (walFile.peek() != std::ifstream::traits_type::eof()) {
        uint32_t len, crc;
        try {
            len = readValue<uint32_t>(walFile);
            crc = readValue<uint32_t>(walFile);
        } catch (const std::runtime_error &) {
            break;
        }

        if (len > MAX_RECORD) break;

        std::string payload(len, '\0');
        walFile.read(payload.data(), static_cast<std::streamsize>(len));
        if (!walFile) break;

        if (crc32(payload) != crc) break;
        validOffset = currentOffset(walFile);

        std::istringstream in(payload, std::ios::binary);
        Record r;
        r.lsn = readValue<uint64_t>(in);
        r.type = readValue<uint8_t>(in);
        r.key = readString(in);

        if (r.type == PUT_OP) {
            r.value = readString(in);
        } else if (r.type != REMOVE_OP) {
            throw std::runtime_error("Unknown op code in wal.");
        }

        if (r.lsn <= checkpointLsn) continue;
        apply(r);
        lastLsn_ = std::max(lastLsn_, r.lsn);
    }

    walFile.close();

    if (static_cast<std::uintmax_t>(validOffset) < std::filesystem::file_size(segment)) {
        std::filesystem::resize_file(segment, static_cast<std::uintmax_t>(validOffset));
        syncFile(segment);
    }
}

/// Iterates in the wal files directory and if the file is valid insert it in the vector.
/// @return an ordered vector that contains the valid wal files path.
std::vector<std::filesystem::path> WAL::discoverSegments() const {
    std::vector<std::filesystem::path> validSegments;

    for (auto const entry: std::filesystem::directory_iterator(walFiesDir)) {
        //before checking that the file name is valid, check if is a dir and if its regular...
        if (std::filesystem::is_regular_file(entry)) {
            //check the filename
            std::string filename(entry.path().filename().string());
            if (!filename.starts_with(PREFIX) || !filename.ends_with(SUFFIX)) {
                continue;
            }
            validSegments.push_back(entry.path());
        }
    }
    std::sort(
        validSegments.begin(),
        validSegments.end(),
        [](const auto &a, const auto &b) {
            return extractSegmentId(a) < extractSegmentId(b);
        }
    );
    return validSegments;
}

/// Retrieve the wal file paths and call replaySegment() for each.
void WAL::replay(
    uint64_t checkpointLsn,
    const std::function<void(const Record &)> &apply) {
    lastLsn_ = checkpointLsn;

    std::vector<std::filesystem::path> segments = discoverSegments();
    for (auto const &segment: segments) {
        replaySegment(segment, checkpointLsn, apply);
    }
}

/// Delete all files in the wal files directory, then generates a fresh wal file
/// (id is not set to 0, instead is serial with the last one).
void WAL::reset() {
    for (auto const &segment: discoverSegments()) {
        std::filesystem::remove(segment);
    }
    currentSegmentId_++;
    createWalFile(currentSegmentId_);
    totalWalSize_ = HEADER_SIZE;
    currentSegmentSize_ = HEADER_SIZE;

    std::cout
    << "CHECKPOINT RESET -> "
    << currentSegmentId_ + 1
    << '\n';
#ifdef __linux__
    syncDirectory(walFiesDir);
#endif
}

/// Logic operation for the "put".
/// Uses a temporary stream buffer to save data, numeric values are inserted using the writeValue()
/// function, string uses writeString().
/// Once the stream is filled, the save is delegated to writeRecord() and the last lsn is incremented by 1.
/// @param key: reference of the inserted key.
/// @param value: reference of the inserted value.
void WAL::appendPut(const std::string &key, const std::string &value) {
    const uint64_t lsn = lastLsn_ + 1;
    std::ostringstream oss(std::ios::binary);
    writeValue(oss, lsn);
    writeValue(oss, PUT_OP);
    writeString(oss, key);
    writeString(oss, value);
    writeRecord(oss.str());
    lastLsn_ = lsn;
}

/// Logic operation for the "remove", see appendPut() for details.
void WAL::appendRemove(const std::string &key) {
    std::ostringstream oss(std::ios::binary);
    const uint64_t lsn = lastLsn_ + 1;
    writeValue(oss, lsn);
    writeValue(oss, REMOVE_OP);
    writeString(oss, key);
    writeRecord(oss.str());
    lastLsn_ = lsn;
}
