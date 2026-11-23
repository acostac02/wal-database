//
// Created by arisu on 18/11/2026.
//

#ifndef KWDB_STORAGEENGINE_H
#define KWDB_STORAGEENGINE_H

#include "../database/database/database.h"
#include "../wal/WAL.h"
#include "../database/user_db/UserDatabase.h"

#include <shared_mutex>


class StorageEngine {
private:
    Database database_;
    UserDatabase usersDb_;
    WAL wal_;

    uint64_t checkpointLsn_ = 0;
    uint64_t checkpointThreshold_ = 0;

    mutable std::shared_mutex mutex_;

    void checkpoint();

    void checkForCheckpoint();

public:
    StorageEngine(
        uint64_t checkpointThreshold,
        uint64_t walSegmentSize
    );

    void put(
        uint64_t ownerId,
        const std::string &key,
        const std::string &value
    );

    bool remove(
        uint64_t ownerId,
        const std::string &key
    );

    std::optional<std::string> get(
        uint64_t ownerId,
        const std::string &key
    ) const;

    bool has(
        uint64_t ownerId,
        const std::string &key
    ) const;

    std::optional<uint64_t> authenticate(
        const std::string &username,
        const std::string &passwordHash
    ) const;

    uint64_t createUser(
        const std::string &username,
        const std::string &passwordHash
    );

    void deleteUser(uint64_t userId);
};


#endif //KWDB_STORAGEENGINE_H
