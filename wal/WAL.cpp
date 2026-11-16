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
    constexpr uint64_t HEADER_SIZE = sizeof(MAGIC) + sizeof(WAL_VERSION) + sizeof(uint64_t);
    const std::string PREFIX = "wal_";
    const std::string SUFFIX = ".log";

    /// Writes the wal file header, composed as: magic "KVWL" | version u32 | first lsn uint64_t
    void writeHeader(std::ofstream &out, uint64_t firstLsn) {
        out.write(MAGIC, sizeof(MAGIC));
        writeValue(out, WAL_VERSION);
        writeValue(out, firstLsn);
    }

    /// Read the wal file header, check magic and version, returns the first lsn
    uint64_t readWalHeader(std::istream &walFile) {
        char magic[4];
        walFile.read(magic, sizeof(magic));
        if (!walFile || memcmp(magic, MAGIC, 4) != 0) {
            throw std::runtime_error("Incorrect magic in wal.");
        }

        const auto version = readValue<uint32_t>(walFile);
        if (version != WAL_VERSION) {
            throw std::runtime_error("wal file version not supported: " + std::to_string(version));
        }

        const auto firstLsn = readValue<uint64_t>(walFile);
        return firstLsn;
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

        if (serial.empty())
            throw std::runtime_error("Invalid segment id: empty serial.");

        for (unsigned char c: serial) {
            if (!std::isdigit(c)) {
                throw std::runtime_error("Invalid character in WAL file name." + filename);
            }
        }

        return std::stoull(serial);
    }
}

/// Constructs and initializes the WAL subsystem.
///
/// The WAL directory is created if it does not already exist.
/// Existing WAL segments are discovered and the segment with the highest identifier is selected as the
/// active segment.
///
/// If no segment is found, the first segment (`wal_000000.log`) is created together with its header.
///
/// The current segment size and the total WAL size are reconstructed from the files present on disk.
/// The active segment is then opened in append mode and kept open for the lifetime of the WAL instance.
///
/// @param path Directory containing WAL segment files.
/// @param maxSegmentSize Maximum size (in bytes) allowed for a single
///        WAL segment before automatic rotation occurs.
///
/// @throws std::runtime_error if the active WAL segment cannot be opened.
WAL::WAL(
    std::filesystem::path path,
    uint64_t maxSegmentSize
)
    : walFilesDir(std::move(path)),
      maxSegmentSize_(maxSegmentSize) {
    std::filesystem::create_directories(walFilesDir);

    auto last = discoverLastSegmentId();
    if (!last) {
        createWalFile(0, 1);
        last = 0;
    }
    currentSegmentId_ = *last;
    currentSegmentSize_ = std::filesystem::file_size(currentSegmentPath());

    currentActiveSegment_.open(
        currentSegmentPath(),
        std::ios::binary | std::ios::app);
    if (!currentActiveSegment_.is_open()) {
        throw std::runtime_error("Cannot open current WAL segment.");
    }

    totalWalSize_ = 0;
    for (auto const &segment: discoverSegments()) {
        totalWalSize_ += std::filesystem::file_size(segment);
    }
}

WAL::~WAL() {
    if (currentActiveSegment_.is_open()) {
        currentActiveSegment_.flush();
        currentActiveSegment_.close();
    }
}


/// Iterates through wal files, returns the last segment id.
std::optional<uint64_t> WAL::discoverLastSegmentId() {
    std::optional<uint64_t> lastId;

    for (auto const dirEntry: std::filesystem::directory_iterator(walFilesDir)) {
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

/// Refresh the current (total) wal files size.
/// Check and set the current segment size and the total wal files size.
void WAL::refreshSizes() {
    currentSegmentSize_ = std::filesystem::file_size(currentSegmentPath());
    totalWalSize_ = 0;
    for (auto const &segment: discoverSegments()) {
        totalWalSize_ += std::filesystem::file_size(segment);
    }
}

/// Build a wal segment path with the id passed as argument.
std::filesystem::path WAL::segmentPath(uint64_t segmentId) const {
    std::ostringstream oss;

    oss << PREFIX
            << std::setw(6)
            << std::setfill('0')
            << segmentId
            << SUFFIX;

    return walFilesDir / oss.str();
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
void WAL::createWalFile(uint64_t segmentId, uint64_t firstLsn) {
    auto walPath = segmentPath(segmentId);

    std::ofstream file(walPath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot create wal segment.");
    }
    writeHeader(file, firstLsn);

    file.flush();
    if (!file) {
        throw std::runtime_error("Cannot write WAL header.");
    }

    file.close();
    syncFile(walPath);
    totalWalSize_ += HEADER_SIZE;
}

void WAL::rotateSegment() {
    currentActiveSegment_.flush();
    if (!currentActiveSegment_) {
        throw std::runtime_error(
            "Cannot flush current WAL segment."
        );
    }
    currentActiveSegment_.close();

    ++currentSegmentId_;
    createWalFile(currentSegmentId_, lastLsn_ + 1);

    currentActiveSegment_.open(
        currentSegmentPath(),
        std::ios::binary | std::ios::app);

    if (!currentActiveSegment_.is_open()) {
        throw std::runtime_error(
            "Cannot open new WAL segment."
        );
    }

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

    std::ostringstream rec(std::ios::binary);
    writeValue(rec, static_cast<uint32_t>(payload.size()));
    writeValue(rec, crc32(payload));
    rec.write(payload.data(), static_cast<std::streamsize>(payload.size()));

    const std::string bytes = rec.str();
    currentActiveSegment_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    currentActiveSegment_.flush();
    if (!currentActiveSegment_) {
        throw std::runtime_error("Cannot write WAL record.");
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

/// Replays a single WAL segment.
///
/// The segment header is validated before iterating over records.
/// Each record is verified through its CRC-32 checksum before being deserialized and applied. Replay stops
/// at the first corrupted, incomplete or truncated record.
///
/// If an invalid record is encountered, the segment is truncated to the last valid offset so that future
/// startups do not repeatedly process the same corruption.
///
/// @param segment Path of the segment being replayed.
/// @param checkpointLsn Snapshot checkpoint LSN.
/// @param apply Callback invoked for every recovered record whose
///        LSN is greater than checkpointLsn.
void WAL::replaySegment(
    const std::filesystem::path &segment,
    uint64_t checkpointLsn,
    const std::function<void(const Record &)> &apply,
    bool isLast
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

    const auto size = std::filesystem::file_size(segment);
    if (static_cast<std::uintmax_t>(validOffset) < size) {
        if (!isLast) {
            throw std::runtime_error("Corrupted record in non-final wal segment: " + segment.string());
        }
        std::filesystem::resize_file(segment, static_cast<std::uintmax_t>(validOffset));
        syncFile(segment);
    }
}

/// Iterates in the wal files directory and if the file is valid insert it in the vector.
/// @return an ordered vector that contains the valid wal files path.
std::vector<std::filesystem::path> WAL::discoverSegments() const {
    std::vector<std::filesystem::path> validSegments;

    for (auto const entry: std::filesystem::directory_iterator(walFilesDir)) {
        //before checking that the file name is valid, check if is a dir and if its regular...
        if (!is_regular_file(entry)) continue;

        try {
            extractSegmentId(entry.path());
            validSegments.push_back(entry.path());

        } catch (const std::exception &e) {
            std::cerr << "ignoring file " << entry.path().filename()
                    << ": " << e.what() << std::endl;
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

/// Replays all WAL segments and restores database state.
///
/// WAL segments are discovered, ordered by segment identifier and processed sequentially from oldest to newest.
/// For each segment, valid records whose LSN is greater than the snapshot checkpoint LSN are deserialized and
/// applied through the callback supplied by the caller.
///
/// Corrupted, incomplete or truncated records are ignored from the first invalid offset onward. The affected
/// segment is automatically truncated to the last known valid position.
///
/// During replay, the highest observed LSN is tracked and stored in lastLsn_ so that newly appended records
/// continue with monotonically increasing sequence numbers.
///
/// @param checkpointLsn Last checkpoint LSN stored in the snapshot.
///        Records with LSN <= checkpointLsn are skipped.
/// @param apply Callback invoked for each recovered record.
///
/// @throws std::runtime_error if a WAL segment cannot be opened, contains an invalid header or contains an unknown
///         operation code.
void WAL::replay(
    uint64_t checkpointLsn,
    const std::function<void(const Record &)> &apply) {
    lastLsn_ = checkpointLsn;

    const auto segments = discoverSegments();
    if (segments.empty()) {
        refreshSizes();
        return;
    }

    for (size_t i = 0; i + 1 < segments.size(); ++i) {
        replaySegment(segments[i], checkpointLsn, apply, false);
    }

    const auto &last = segments.back();
    if (std::filesystem::file_size(last) < HEADER_SIZE) {
        {
            std::ifstream file(last, std::ios::binary);
            char magic[4] = {};
            file.read(magic, sizeof(magic));
            const auto got = static_cast<size_t>(file.gcount());

            if (got > 0 && memcmp(magic, MAGIC, got) != 0) {
                throw std::runtime_error("Incorrect magic in last wal segment.");
            }
        }

        std::filesystem::remove(last);
        createWalFile(currentSegmentId_, lastLsn_ + 1);
        syncDirectory(walFilesDir);
    } else {
        replaySegment(last, checkpointLsn, apply, true);
    }

    refreshSizes();
}

/// Resets the WAL after a successful checkpoint.
///
/// The currently active segment is flushed and closed. A new WAL segment is then created using the next
/// segment identifier and opened as the active segment.
///
/// All older WAL segments are removed because their changes are already reflected in the persisted snapshot.
/// The total WAL size and current segment size are reset to the size of a segment containing only its header.
///
/// On POSIX systems, the WAL directory metadata is synchronized after segment cleanup to improve durability
/// of file creation and deletion operations.
///
/// The current LSN is intentionally preserved so that newly appended records continue with monotonically
/// increasing LSNs.
///
/// @throws std::runtime_error on segment creation/open failures.
void WAL::reset() {
    currentActiveSegment_.flush();
    if (!currentActiveSegment_) {
        throw std::runtime_error("Cannot flush current wal segment.");
    }
    currentActiveSegment_.close();

    ++currentSegmentId_;
    createWalFile(currentSegmentId_, lastLsn_ + 1);

    for (auto const &segment: discoverSegments()) {
        if (extractSegmentId(segment) < currentSegmentId_) {
            std::error_code ec;
            std::filesystem::remove(segment, ec);
            if (ec) {
                std::cerr << "Cannot remove old wal segment: " << ec.message() << std::endl;
            }
        }
    }

#ifndef _WIN32
    syncDirectory(walFilesDir);
#endif

    currentActiveSegment_.open(
        currentSegmentPath(),
        std::ios::binary | std::ios::app
        );

    if (!currentActiveSegment_.is_open()) {
        throw std::runtime_error("Cannot open current wal segment.");
    }

    totalWalSize_ = HEADER_SIZE;
    currentSegmentSize_ = HEADER_SIZE;
}

/// Logic operation for the "put".
/// Uses a temporary stream buffer to save data, numeric values are inserted using the writeValue() function,
/// string uses writeString().
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
