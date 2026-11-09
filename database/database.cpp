//
// Created by arisu on 06/11/2026.
//

#include "../database/database.h"

#include <cstring>
#include "../wal/WAL.h"
#include "../format.h"
#include "iostream"
#include <fstream>

using namespace format;

namespace {
    constexpr char MAGIC[4] = {'K', 'V', 'D', 'B'};
    struct Header {
        uint32_t version = 1;
        uint64_t checkpoint_lsn = 0;
        uint64_t count = 0;
    };

    //write the database file header into the specified ofstream
    void writeHeader(std::ofstream& out, const Header& header) {
        out.write(MAGIC, sizeof(MAGIC));
        writeValue(out, header.version);
        writeValue(out, header.checkpoint_lsn);
        writeValue(out, header.count);
    }

    //read the database file header, first checl
    Header readHeader(std::ifstream& in) {
        char magic[4];
        in.read(reinterpret_cast<char*>(magic), sizeof(magic));
        if (memcmp(magic, MAGIC, 4) != 0) {
            throw std::runtime_error("Incorrect magic in database.");
        }

        Header h{};
        h.version = readValue<uint32_t>(in);
        if (h.version != 1) {
            throw std::runtime_error("Incorrect version in database.");
        }
        h.checkpoint_lsn = readValue<uint64_t>(in);
        h.count = readValue<uint64_t>(in);
    }
}


//Initialize database, create database file if not exist and insert the first header with version 1, wal checkpoint 0 and couples count 0.
Database::Database()
    : dbPath("../dbfile/dbfile.db") {
    std::filesystem::create_directories(dbPath.parent_path());
    if (!std::filesystem::exists(dbPath)) {
        std::ofstream file(dbPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open database file for initialization.");
        }

        writeHeader(file, Header{});
        file.flush();
        if (!file)
            throw std::runtime_error("Cannot write header in database file.");
        file.close();
    }

    loadFromDisk();
    //wal.replay(checkpointLsn, /* apply */);
}

//Insert a pair key-value in the unsorted_map
void Database::put(
    const std::string &key,
    const std::string &value) {
    wal.appendPut(key, value);
    data[key] = value;
}

//Get the value from the key
std::string Database::get(
    const std::string &key)
const {
    return data.at(key);
}

//Remove a key-value pair from the unsorted_map
void Database::remove(
    const std::string &key) {
    if (data.contains(key)) {
        wal.appendRemove(key);
        data.erase(key);
    }
}

//Persist header and pairs (present in the data map) in the database file.
void Database::saveToDisk() {
    std::ofstream file(dbPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open database file for dump.");
    }

    Header header;
    header.checkpoint_lsn = checkpointLsn;
    header.count = data.size();
    writeHeader(file, header);

    for (const auto& [key, value] : data) {
        writeString(file, key);
        writeString(file, value);
    }
    file.flush();
    if (!file) {
        throw std::runtime_error("Cannot write database file.");
    }
    file.close();
}

// Loads data from the database file into the unordered_map.
void Database::loadFromDisk() {

    std::ifstream file(dbPath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open database file for load.");
    }

    Header header = readHeader(file);
    checkpointLsn = header.checkpoint_lsn;
    data.clear();

    for (uint64_t i = 0; i < header.count; i++) {
        std::string key = readString(file);
        std::string value = readString(file);
        data[std::move(key)] = std::move(value);
    }
}
