//
// Created by arisu on 06/11/2026.
//

#include "wal.h"
#include "../database/database.h"
#include "../format.h"

#include <filesystem>
#include "fstream"
#include <cstring>

#include "../fileoperations/fileio.h"

using namespace format;

namespace {
    constexpr uint32_t MAX_RECORD = 64 * 1024 * 1024; //64MB
    const char MAGIC[4] = {'K', 'V', 'W', 'L'};
    constexpr uint32_t WAL_VERSION = 1;
    constexpr uint64_t HEADER_SIZE = sizeof(MAGIC) + sizeof(WAL_VERSION);

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
}

/// Wal class constructor, generate the wal file if not exist (zero-sized file is also considered as invalid) and insert
/// the header (MAGIC + WAL_VERSION).
/// @param path the path where the file is created.
/// @throws std::runtime_error if the created file cannot be opened.
/// @throws std::runtime_error if the header cannot be written in the created file.
WAL::WAL(std::filesystem::path path)
    : walPath(std::move(path)) {
    if (!walPath.parent_path().empty())
        std::filesystem::create_directories(walPath.parent_path());

    if (!std::filesystem::exists(walPath) || std::filesystem::file_size(walPath) == 0) {
        std::ofstream out(walPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            throw std::runtime_error("Cannot open WAL file for initialization.");
        }

        writeHeader(out);
        out.flush();
        if (!out) {
            throw std::runtime_error("Cannot write WAL header.");
        }
        out.close();
        if (out.fail()) {
            throw std::runtime_error("Cannot close wal file after the header write.");
        }
        syncFile(walPath);
    }

    walSize_ = std::filesystem::file_size(walPath);
}

/// Getter for wal size
uint64_t WAL::getWalSize() const {
    return walSize_;
}


/// Appends one record to the WAL file, in append mode.
/// Record layout: [payload size: uint32][CRC-32 of payload: uint32][payload].
/// The record is assembled in memory and written with a single write call.
/// @param payload Serialized operation (lsn, op code, key, value).
/// @throws std::runtime_error if the file cannot be opened or written.
void WAL::writeRecord(const std::string &payload) {
    std::ofstream out(walPath, std::ios::binary | std::ios::app);
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

    syncFile(walPath);
    walSize_ += bytes.size();
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
void WAL::replay(
    uint64_t checkpointLsn,
    const std::function<void(const Record &)> &apply) {
    std::ifstream walFile(walPath, std::ios::binary);
    if (!walFile.is_open()) {
        throw std::runtime_error("Cannot open WAL file for replay.");
    }

    readWalHeader(walFile);
    lastLsn_ = checkpointLsn;

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
    if (static_cast<std::uintmax_t>(validOffset) < std::filesystem::file_size(walPath)) {
        std::filesystem::resize_file(walPath, static_cast<std::uintmax_t>(validOffset));
        syncFile(walPath);
    }

    walSize_ = std::filesystem::file_size(walPath);
}

/// Truncates the WAL to its header, discarding all records.
/// Must be called only after a snapshot with checkpoint_lsn >= lastLsn()
/// has been durably written, otherwise the discarded operations are lost.
/// lastLsn_ is intentionally preserved so new records keep increasing LSNs.
/// @throws std::filesystem::filesystem_error or std::runtime_error on failure.
void WAL::reset() {
    std::filesystem::resize_file(walPath, HEADER_SIZE);
    syncFile(walPath);
    walSize_ = HEADER_SIZE;
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
