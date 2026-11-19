//
// Created by arisu on 18/11/2026.
//

#include "StorageEngine.h"

#include <iostream>
#include <mutex>

/**
 * @brief Constructs the storage engine and restores its state.
 *
 * Initializes the user database, key-value database and WAL components, then replays all WAL records
 * newer than the last checkpoint in order to reconstruct the most recent consistent state.
 *
 * @param checkpointThreshold WAL size threshold that triggers a checkpoint (sum of all the wal segments).
 * @param walSegmentSize Maximum size, in bytes, of a single WAL segment.
 */
StorageEngine::StorageEngine(
    const uint64_t checkpointThreshold,
    const uint64_t walSegmentSize
)
    : database_(),
      usersDb_("../dbfile/users.db"),
      wal_("../dbfile/walfiles", walSegmentSize),
      checkpointThreshold_(checkpointThreshold) {
    checkpointLsn_ = database_.checkpointLsn();

    wal_.replay(
        checkpointLsn_,
        [this](const Record &r) {
            switch (r.type) {
                case RecordType::PutKV:
                    database_.put(
                        r.ownerId,
                        r.key,
                        r.value
                    );
                    break;

                case RecordType::RemoveKV:
                    database_.remove(
                        r.ownerId,
                        r.key
                    );
                    break;

                case RecordType::CreateUser:
                    usersDb_.applyCreateUser(
                        r.ownerId,
                        r.key,
                        r.value
                    );
                    break;

                case RecordType::DeleteUser:
                    usersDb_.applyDeleteUser(
                        r.ownerId
                    );

                    database_.deleteAllForUser(
                        r.ownerId
                    );
                    break;
            }
        });
}

/**
 * @brief Evaluate if the wal file should make the checkpoint.
 * If the actual (total) wal size >= threshold the checkpoint function is called.
 * Catches the runtime error thrown by the checkpoint() function if a problem occurs and print
 * the details in the terminal.
 */
void StorageEngine::checkForCheckpoint() {
    if (wal_.getTotalWalSize() < checkpointThreshold_)
        return;
    try {
        checkpoint();
    } catch (std::runtime_error &e) {
        std::cerr << "checkpoint failed: " << e.what() << std::endl;
    }
}

/**
 * @brief Creates a database checkpoint.
 *
 * The operation is protected by an exclusive lock to prevent concurrent reads and writes while the
 * checkpoint is being built.
 * The current in-memory state is first atomically persisted into the snapshot file.
 * Once the snapshot is durably written, the WAL is reset and a new segment is created.
 */
void StorageEngine::checkpoint() {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    checkpointLsn_ = wal_.getLastLsn();

    database_.saveToDisk(checkpointLsn_);
    usersDb_.saveToDisk();

    wal_.reset();
}

/**
 * @brief Inserts or updates a key-value pair.
 *
 * The operation is first appended to the WAL to guarantee durability, then applied to the in-memory
 * database state. After the update, the checkpoint threshold is evaluated.
 *
 * @param ownerId Identifier of the user that owns the key-value pair.
 * @param key Key to insert or update.
 * @param value Value associated with the key.
 */
void StorageEngine::put(const uint64_t ownerId, const std::string &key, const std::string &value) {
    wal_.appendPut(ownerId, key, value);
    database_.put(ownerId, key, value);
    checkForCheckpoint();
}

/**
 * @brief Removes a key-value pair.
 *
 * The remove operation is first appended to the WAL and then applied to the key-value database.
 * Once completed, the checkpoint condition is evaluated.
 *
 * @param ownerId Identifier of the user performing the operation.
 * @param key Key to remove.
 */
void StorageEngine::remove(const uint64_t ownerId, const std::string &key) {
    wal_.appendRemove(ownerId, key);
    database_.remove(ownerId, key);
    checkForCheckpoint();
}

/**
 * @brief Retrieves the value associated with a key.
 *
 * Searches the key-value database and returns the associated value if the key exists
 * and belongs to the specified user.
 *
 * @param ownerId Identifier of the key owner.
 * @param key Key to retrieve.
 * @return The associated value, or std::nullopt if the key is not found.
 */
std::optional<std::string> StorageEngine::get(
    uint64_t ownerId,
    const std::string &key
) const {
    return database_.get(ownerId, key);
}

/**
 * @brief Checks whether a key exists.
 *
 * Verifies whether the specified user owns the given key in the key-value database.
 *
 * @param ownerId Identifier of the key owner.
 * @param key Key to search for.
 * @return true if the key exists, false otherwise.
 */
bool StorageEngine::has(
    uint64_t ownerId,
    const std::string &key
) const {
    return database_.has(ownerId, key);
}

/**
 * @brief Authenticates a user.
 *
 * Validates the provided username and password hash against the user database and returns
 * the corresponding user identifier when authentication succeeds.
 *
 * @param username Username to authenticate.
 * @param passwordHash Password hash associated with the account.
 * @return The authenticated user identifier, or std::nullopt if
 *         authentication fails.
 */
std::optional<uint64_t> StorageEngine::authenticate(
    const std::string &username,
    const std::string &passwordHash
) const {
    return usersDb_.authenticate(
        username,
        passwordHash
    );
}

/**
 * @brief Creates a new user account.
 *
 * A new user identifier is reserved, the operation is recorded in the WAL for durability,
 * and the user is then added to the user database. Finally, the checkpoint condition is evaluated.
 *
 * @param username Username associated with the new account.
 * @param passwordHash Password hash associated with the account.
 * @return The identifier assigned to the newly created user.
 */
uint64_t StorageEngine::createUser(
    const std::string &username,
    const std::string &passwordHash) {

    const uint64_t id = usersDb_.reserveUserId();

    wal_.appendCreateUser(
        id,
        username,
        passwordHash
    );

    usersDb_.createUser(
        id,
        username,
        passwordHash
    );

    checkForCheckpoint();

    return id;
}

/**
 * @brief Deletes a user account and all associated data.
 *
 * The delete operation is first appended to the WAL and then applied to the in-memory state.
 * All key-value pairs owned by the user are removed from the database and the user account
 * is deleted from the user database. Finally, the checkpoint condition is evaluated.
 *
 * @param userId Identifier of the user to delete.
 */
void StorageEngine::deleteUser(uint64_t userId) {
    wal_.appendDeleteUser(userId);

    database_.deleteAllForUser(userId);
    usersDb_.deleteUser(userId);

    checkForCheckpoint();
}
