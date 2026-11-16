//
// Created by arisu on 06/11/2026.
//

#include "../database/database.h"

#include "../wal/WAL.h"
#include "../format.h"
#include "../fileoperations/fileio.h"

#include <cstring>
#include "iostream"
#include <fstream>
#include <mutex>

using namespace format;

namespace {
    constexpr char MAGIC[4] = {'K', 'V', 'D', 'B'};

    struct Header {
        uint32_t version = 1;
        uint64_t checkpoint_lsn = 0;
        uint64_t count = 0;
    };


    /// Write the database file header into the specified ofstream.
    /// @param out reference of the file stream where to write the header.
    /// @param header reference if the header struct.
    void writeHeader(std::ofstream &out, const Header &header) {
        out.write(MAGIC, sizeof(MAGIC));
        writeValue(out, header.version);
        writeValue(out, header.checkpoint_lsn);
        writeValue(out, header.count);
    }

    /// Read the database file header, check magic and version and set the struct filed.
    /// @param in input file stream.
    /// @throws std::runtime_error if the magic / version is not correct.
    Header readHeader(std::ifstream &in) {
        char magic[4];
        in.read(reinterpret_cast<char *>(magic), sizeof(magic));
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
        return h;
    }
}


/// Database constructor, writes the header into the db file, load last snapshot from disk and replay the wal file.
/// @throws std::runtime_error if the db file cannot be opened for the initialization or if the header cannot be written.
Database::Database(
    std::uint64_t chkpThreshold,
    uint64_t walSegmentSize
)
    : dbPath("../dbfile/dbfile.db"),
      wal(
          dbPath.parent_path() / "walfiles",
          walSegmentSize
      ),
      checkpointThreshold(chkpThreshold) {
    //4MB
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
    wal.replay(checkpointLsn, [this](const WAL::Record &r) {
        if (r.type == WAL::PUT_OP) data[r.key] = r.value;
        else if (r.type == WAL::REMOVE_OP) data.erase(r.key);
    });
}

/// Inserts or updates a key-value pair.
///
/// The operation is first appended to the WAL and durably persisted  before the in-memory state is updated.
/// If the key already exists, its value is replaced.
///
/// A checkpoint may be triggered automatically if the accumulated WAL size exceeds the configured threshold.
///
/// @param key Key to insert or update.
/// @param value Value associated with the key.
///
/// @throws std::runtime_error if WAL persistence fails.
void Database::put(
    const std::string &key,
    const std::string &value) {
    std::unique_lock<std::shared_mutex> lock(dbMutex_);

    wal.appendPut(key, value);
    data[key] = value;
    checkForCheckpoint();
}

/// Returns the value associated with a key.
///
/// The lookup is performed on the in-memory store and does not access disk. Multiple concurrent readers may
/// execute this operation simultaneously.
///
/// @param key Key to search.
///
/// @return The value associated with the key.
///
/// @throws std::runtime_error if the key does not exist.
std::string Database::get(const std::string &key) const {
    std::shared_lock<std::shared_mutex> lock(dbMutex_);

    const auto it = data.find(key);
    if (it == data.end()) {
        throw std::runtime_error("Key not found in database.");
    }
    return it->second;
}

/// Check if a key is present in the database.
///
/// The lookup is performed on the in-memory store and does not access disk. Multiple concurrent readers may
/// execute this operation simultaneously.
///
/// @param key Key to search.
///
/// @return true if the key is present, false otherwise.
bool Database::has(const std::string &key) const {
    std::shared_lock<std::shared_mutex> lock(dbMutex_);

    return data.contains(key);
}

/// Removes a key from the database.
///
/// If the key exists, a REMOVE record is durably appended to the WAL before the entry is erased from
/// the in-memory store. If the key does not exist, the operation has no effect.
///
/// A checkpoint may be triggered automatically if the accumulated WAL size exceeds the configured threshold.
///
/// @param key Key to remove.
///
/// @throws std::runtime_error if WAL persistence fails.
void Database::remove(const std::string &key) {
    std::unique_lock<std::shared_mutex> lock(dbMutex_);

    if (data.contains(key)) {
        wal.appendRemove(key);
        data.erase(key);
        checkForCheckpoint();
    }
}

/// Persist header and pairs (present in the data map) in the database file.
/// data is initially saved in the .db.tmp file, then the atomic function atomicReplace() overwrite the old .db file
/// with the new one.
/// @throws std::runtime_error if the file cannot be opened, wrote or closed.
/// @throws std::error_code ec if an error occurs during the replacement.
void Database::saveToDisk() {
    std::filesystem::path tmpPath = dbPath;
    tmpPath += ".tmp";

    std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open database file for dump.");
    }

    Header header;
    header.checkpoint_lsn = wal.getLastLsn();
    header.count = data.size();
    writeHeader(file, header);

    for (const auto &[key, value]: data) {
        writeString(file, key);
        writeString(file, value);
    }
    file.flush();

    if (!file) {
        throw std::runtime_error("Cannot write database file.");
    }

    file.close();
    if (file.fail())
        throw std::runtime_error("Cannot close database file.");

    try {
        atomicReplace(tmpPath, dbPath);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(tmpPath, ec);
        throw;
    }
    checkpointLsn = header.checkpoint_lsn;
}

///Loads data from the database file into the unordered_map.
void Database::loadFromDisk() {
    std::ifstream file(dbPath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open database file for load.");
    }

    const Header header = readHeader(file);
    checkpointLsn = header.checkpoint_lsn;
    data.clear();

    for (uint64_t i = 0; i < header.count; i++) {
        std::string key = readString(file);
        std::string value = readString(file);
        data[std::move(key)] = std::move(value);
    }
}

/// Evaluate if the wal file should make the checkpoint.
/// The checkpoint threshold is set in the Database constructor (into the private
/// field checkPointThreshold).
/// If the actual wal size >= threshold checkpoint function is called.
void Database::checkForCheckpoint() {
    if (wal.getTotalWalSize() < checkpointThreshold)
        return;
    try {
        checkpoint();
    } catch (std::runtime_error &e) {
        std::cerr << "checkpoint failed: " << e.what() << std::endl;
    }
}

/// Creates a database checkpoint.
///
/// The operation is protected by an exclusive lock to prevent
/// concurrent reads and writes while the checkpoint is being built.
///
/// First, the current in-memory state is atomically persisted into
/// the snapshot file. Once the snapshot is durably written, the WAL
/// is reset and a new segment is created.
void Database::checkpoint() {
    std::unique_lock<std::shared_mutex> lock(dbMutex_);

    saveToDisk();
    wal.reset();
}
