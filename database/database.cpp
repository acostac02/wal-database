//
// Created by arisu on 06/11/2026.
//

#include "../database/database.h"

#include "../wal/WAL.h"
#include "../format.h"
#include "../file_operations/fileio.h"

#include <cstring>
#include "iostream"
#include <fstream>
#include <mutex>

using namespace format;

namespace {
    constexpr char MAGIC[4] = {'K', 'V', 'D', 'B'};

    /**
    * Write the database file header into the specified output stream
    *
    * The header consists in:
    * - a sequence of bytes to identify the file type (magic);
    * - actual version of the db file;
    * - checkpoint lsn (log sequence number, identifier of the last transaction);
    * - the number of pairs inserted (count).
    *
    * @param out a reference of the file stream where to write the header.
    * @param header a reference of the header struct.
     */
    void writeHeader(std::ofstream &out, const Header &header) {
        out.write(MAGIC, sizeof(MAGIC));
        writeValue(out, header.version);
        writeValue(out, header.checkpoint_lsn);
        writeValue(out, header.count);
    }

    /**
     * Function to read the database file header.
     *
     * Evaluate a correct header by checking the magic bytes and the file version.
     *
     * @param in reference to the input file stream
     * @returns a Header struct
     * @throws std::runtime_error if verification fails
     */
    Header readHeader(std::ifstream &in) {
        char magic[4];
        in.read(reinterpret_cast<char *>(magic), sizeof(magic));
        if (memcmp(magic, MAGIC, 4) != 0) {
            throw std::runtime_error("Incorrect magic in database.");
        }

        Header h{};
        h.version = readValue<uint32_t>(in);
        if (h.version != 2) {
            throw std::runtime_error("Incorrect version in database.");
        }
        h.checkpoint_lsn = readValue<uint64_t>(in);
        h.count = readValue<uint64_t>(in);
        return h;
    }
}

/**
 * Database object constructor.
 *
 * Initializes the database file path field, if the file does not exist generates a fresh one
 * and insert the header.
 * After the check, if present, loads the key value pairs in the in-memory map (loadFromDisk()).
 *
 * @throws std::runtime_error if the db file cannot be opened for the initialization or if the
 *                            header cannot be written.
 */
Database::Database()
    : dbPath_("../dbfile/dbfile.db"){
    //4MB
    std::filesystem::create_directories(dbPath_.parent_path());
    if (!std::filesystem::exists(dbPath_)) {
        std::ofstream file(dbPath_, std::ios::binary | std::ios::trunc);
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
}

/**
 * @brief Inserts or updates a key-value pair.
 *
 * Acquires the unique lock and insert the pair in the data map.
 *
 * @param ownerId the key-value owner identifier.
 * @param key Key to insert or update.
 * @param value Value associated with the key.
 */
void Database::put(
    uint64_t ownerId,
    const std::string &key,
    const std::string &value
    ) {
    std::unique_lock<std::shared_mutex> lock(dbMutex_);
    data_[Key{ownerId, key}] = value;
}

/**
 * @brief Returns the value associated with a key.
 *
 * The lookup is performed on the in-memory store and does not access disk. Multiple concurrent readers may
 * execute this operation simultaneously by acquiring the shared lock.
 *
 * @param key Key to search.
 *
 * @return The value associated with the key (if fount), nullopt otherwise.
 */
std::optional<std::string> Database::get(
    uint64_t ownerId,
    const std::string &key
) const {
    std::shared_lock<std::shared_mutex> lock(dbMutex_);

    const auto it = data_.find(Key{ownerId, key});
    if (it == data_.end()) {
        return std::nullopt;
    }
    return it->second;
}

/**
 * @brief Check if a key is present in the database.
 *
 * The lookup is performed on the in-memory store and does not access disk. Multiple concurrent readers may
 * execute this operation simultaneously by acquiring the shared lock.
 *
 * @param key Key to search.
 *
 * @return true if the key is present, false otherwise.
 */
bool Database::has(uint64_t ownerId, const std::string &key) const {
    std::shared_lock<std::shared_mutex> lock(dbMutex_);

    return data_.contains(Key{ownerId, key});
}

/**
 * @brief Removes a key from the database.
 *
 * Acquires the unique lock, search and remove the ket-value pair from the data map (if present).
 *
 * @param ownerId the identifier of the user that want to execute the operation.
 * @param key reference of the key to remove.
 */
void Database::remove(const uint64_t ownerId, const std::string &key) {
    std::unique_lock<std::shared_mutex> lock(dbMutex_);

    if (data_.contains(Key{ownerId, key})) {
        data_.erase(Key{ownerId, key});
    }
}

/**
 * @brief Deletes all the user-related key-value pairs.
 *
 * Acquires the unique lock, iterates on the data map and erase the element if the condition
 * is respected.
 *
 * @param userId the user unique identifier.
*/
void Database::deleteAllForUser(const uint64_t userId) {
    std::unique_lock lock(dbMutex_);
    for (auto it = data_.begin(); it != data_.end();) {
        if (it->first.ownerId == userId) {
            it = data_.erase(it);
        }
        else {
            ++it;
        }
    }
}

/**
 * @brief Getter for the last loaded checkpoint lsn.
 */
uint64_t Database::checkpointLsn() const {
    return loadedCheckpointLsn_;
}


/**
 * @brief Persists the current key-value snapshot to disk.
 *
 * The database content is written to a temporary file and atomically
 * replaces the previous snapshot once the write completes successfully.
 *
 * @param checkpointLsn the last checkpoint lsn
 *
 * @throws std::runtime_error if the file cannot be opened, wrote or closed.
 * @throws std::error_code ec if an error occurs during the replacement.
 */
void Database::saveToDisk(uint64_t checkpointLsn) {
    std::filesystem::path tmpPath = dbPath_;
    tmpPath += ".tmp";

    std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open database file for dump.");
    }

    Header header;
    header.checkpoint_lsn = checkpointLsn;
    header.count = data_.size();
    writeHeader(file, header);

    for (const auto &[k, value]: data_) {
        writeValue(file,k.ownerId);
        writeString(file,k.key);
        writeString(file,value);
    }
    file.flush();

    if (!file) {
        throw std::runtime_error("Cannot write database file.");
    }

    file.close();
    if (file.fail())
        throw std::runtime_error("Cannot close database file.");

    try {
        atomicReplace(tmpPath, dbPath_);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(tmpPath, ec);
        throw;
    }
}

/**
 * @brief fills the in-memory map with the data persisted on disk.
 *
 * Open the database file, reads the header (checking the integrity and correctness) and then iterates
 * from 0 to the pairs persisted (using the header filed "count") loading the data in the map.
 *
 * @throws std::runtime_error if the file cannot be opened
 */
void Database::loadFromDisk() {
    std::ifstream file(dbPath_, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open database file for load.");
    }

    const Header header = readHeader(file);
    loadedCheckpointLsn_ = header.checkpoint_lsn;
    data_.clear();

    for (uint64_t i = 0; i < header.count; i++) {
        const auto ownerId = readValue<uint64_t>(file);
        std::string key = readString(file);
        std::string value = readString(file);
        data_[Key{ownerId, std::move(key)}] = std::move(value);
    }
}

