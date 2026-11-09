//
// Created by arisu on 06/11/2026.
//

#include "wal.h"
#include "../database/database.h"
#include "../format.h"

#include <filesystem>
#include "fstream"

using namespace format;

namespace {
    const char MAGIC[4] = {'K', 'V', 'W', 'L'};
    constexpr uint64_t WAL_VERSION = 1;

    void writeHeader(std::ofstream &out) {
        out.write(MAGIC, sizeof(MAGIC));
        writeValue(out, WAL_VERSION);
    }
}

//constructor, generate the wal file if not exist.
WAL::WAL(std::filesystem::path path)
    : walPath(std::move(path)) {
    std::filesystem::create_directory(path.parent_path());

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
    }
}

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
}

uint64_t WAL::getLastLsn() const {
    return lastLsn_;
}

//First implementation of WAL recovery (for example after a crash).
//Load wal file and insert in memory the lines content.
//This first version does not use checkpoint, only for testing.
void WAL::replay(Database &db) {

}

void WAL::appendPut(const std::string &key, const std::string &value) {
    std::ostringstream oss(std::ios::binary);
    writeValue(oss, lastLsn_+1);
    writeValue(oss, PUT_OP);
    writeString(oss, key);
    writeString(oss, value);
    writeRecord(oss.str());
    lastLsn_++;
}

void WAL::appendRemove(const std::string &key) {
    std::ostringstream oss(std::ios::binary);
    writeValue(oss, lastLsn_+1);
    writeValue(oss, REMOVE_OP);
    writeValue(oss, key);
    writeRecord(oss.str());
    lastLsn_++;
}
