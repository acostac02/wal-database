//
// Created by arisu on 10/11/2026.
//

#include <cassert>
#include <iostream>
#include <stdexcept>

#include "../database/database.h"

namespace {
    const std::filesystem::path DB_DIR = "../dbfile";
    const std::filesystem::path WAL_PATH = DB_DIR / "wal.log";
    const std::filesystem::path WAL_BACKUP = DB_DIR / "wal.backup";

    void printState(Database& db) {
        for (auto k : {"a", "b", "c"})
            std::cout << k << (db.has(k) ? " present\n" : " absent\n");
    }

    /// Copies src over dst. Removes dst first because overwrite_existing
    /// fails on some MinGW/libstdc++ versions on Windows.
    void replaceFile(const std::filesystem::path& src,
                     const std::filesystem::path& dst) {
        std::error_code ec;
        std::filesystem::remove(dst, ec);
        if (ec)
            throw std::runtime_error("Cannot remove " + dst.string() + ": " + ec.message());
        std::filesystem::copy_file(src, dst);
    }
}

/// Crash after a checkpoint: "c" and remove("a") live only in the WAL.
void testA() {
    std::cout << "=== testA ===\n";

    if (!std::filesystem::exists(DB_DIR)) {
        std::cout << "INCORRECT db file dir.";
    }

    std::filesystem::remove_all(DB_DIR);   // clean state

    {
        Database db;
        db.put("a", "1");
        db.put("b", "2");
        db.saveToDisk();      // checkpoint_lsn = 2

        db.put("c", "3");     // WAL only (lsn 3)
        db.remove("a");       // WAL only (lsn 4)
        // no saveToDisk: simulated crash, RAM is lost here
    }

    {
        Database db;          // snapshot + replay
        printState(db);
        assert(!db.has("a"));   // REMOVE replayed
        assert(db.has("b"));    // from snapshot
        assert(db.has("c"));    // from WAL
    }
}

/// Crash in the middle of the last record: remove("a") is lost.
/// Depends on testA having just run, so call it right after testA().
/// Crash in the middle of the last record: remove("a") is lost, the broken
/// tail is truncated, and new writes must survive another restart.
/// Depends on testA having just run, so call it right after testA().
void testB() {
    std::cout << "=== testB ===\n";

    const auto size = std::filesystem::file_size(WAL_PATH);
    std::filesystem::resize_file(WAL_PATH, size - 3);   // simulate crash mid-write

    {
        Database db;              // snapshot + replay, truncates the broken record
        printState(db);
        assert(db.has("a"));      // remove (lsn 4) was cut, not applied
        assert(db.has("b"));
        assert(db.has("c"));      // lsn 3 intact

        db.put("d", "4");         // must land right after the last valid record
    }

    {
        Database db;              // second restart
        std::cout << "--- after put d and restart ---\n";
        for (auto k : {"a", "b", "c", "d"})
            std::cout << k << (db.has(k) ? " present\n" : " absent\n");
        assert(db.has("a"));
        assert(db.has("b"));
        assert(db.has("c"));
        assert(db.has("d"));      // lost if the broken tail was not truncated
    }
}

/// Verifies WAL recovery against truncation at every possible byte offset.
///
/// Writes N records (k1..kN) into a fresh WAL with an empty snapshot
/// (checkpoint_lsn = 0) and saves a backup of the intact file. Then, for
/// every cut length between the end of the WAL header and the end of the
/// last record:
///   1. restores the intact WAL from the backup;
///   2. truncates it to `cut` bytes, simulating a crash in the middle of
///      a write;
///   3. opens a Database (snapshot + replay + tail truncation);
///   4. checks that exactly the records fully contained in `cut` bytes
///      are present (k1..km) and all the following ones are absent.
///
/// A truncated record (inside len, crc or payload) must behave exactly
/// like a record that was never written. The test aborts printing the
/// failing cut offset and key.
void testWalTruncationAfterEveryOffset() {
    std::cout << "=== testWalTruncationAfterEveryOffset ===\n";
    constexpr int N = 5;
    std::uintmax_t headerEnd = 0;
    std::vector<std::uintmax_t> ends;

    std::filesystem::remove_all(DB_DIR);
    {
        Database db;
        headerEnd = std::filesystem::file_size(WAL_PATH);
        for (int i = 1; i <= N; ++i) {
            db.put("k" + std::to_string(i), "v" + std::to_string(i));
            ends.push_back(std::filesystem::file_size(WAL_PATH));
        }
    }
    replaceFile(WAL_PATH, WAL_BACKUP);

    for (std::uintmax_t cut = headerEnd; cut <= ends.back(); ++cut) {
        replaceFile(WAL_BACKUP, WAL_PATH);
        std::filesystem::resize_file(WAL_PATH, cut);

        int m = 0;
        for (auto e : ends)
            if (e <= cut) ++m;

        Database db;
        for (int i = 1; i <= N; ++i) {
            const bool expected = (i <= m);
            const bool actual = db.has("k" + std::to_string(i));
            if (actual != expected) {
                std::cerr << "FAIL at cut=" << cut << " key=k" << i
                          << " expected=" << expected << '\n';
                std::abort();
            }
        }
    }
    std::cout << "ok\n";
}

/// Verifies how the WAL handles a file truncated inside its own header.
///
/// Writes N records into a fresh WAL, saves a backup of the intact file,
/// then for every cut length from 0 to 7 bytes (shorter than the 8-byte
/// header: magic + version):
///   1. restores the intact WAL from the backup;
///   2. truncates it to `cut` bytes;
///   3. tries to open a Database.
///
/// Expected behavior:
///   - cut == 0: the empty file is treated as a new WAL, the Database
///     opens without errors and contains no keys;
///   - 1 <= cut <= 7: the header is incomplete, the file is considered
///     corrupted and the constructor must throw std::runtime_error.
///
/// The test aborts printing the failing cut offset if the outcome differs.
void testWalTruncationInsideHeader() {
    std::cout << "=== testWalTruncationInsideHeader ===\n";
    constexpr int N = 5;

    std::filesystem::remove_all(DB_DIR);
    {
        Database db;
        for (int i = 1; i <= N; ++i) {
            db.put("k" + std::to_string(i), "v" + std::to_string(i));
        }
    }
    replaceFile(WAL_PATH, WAL_BACKUP);

    for (uintmax_t cut = 0; cut <= 7; ++cut) {
        replaceFile(WAL_BACKUP, WAL_PATH);
        std::filesystem::resize_file(WAL_PATH, cut);

        bool threw = false;

        try {
            Database db;
            if (cut == 0) { //void wal file, db should not have k in the map.
                for (int i = 1; i <= N; ++i) {
                    if (db.has("k" + std::to_string(i))) {
                        std::cerr << "FAIL at cut=0: k" << i << " should be absent\n";
                        std::abort();
                    }
                }
            }
        } catch (const std::runtime_error&) {
            threw = true;
        }

        const bool expectThrow = (cut != 0);
        if (threw != expectThrow) {
            std::cerr << "FAIL at cut=" << cut << " threw=" << threw
                      << " expected=" << expectThrow << '\n';
            std::abort();
        }

        std::cout << "Iteration " << std::to_string(cut) << " OK" << std::endl;
    }
}

void walCheckpointTest() {
    std::cout << "=== walCheckpointTest ===\n";

    constexpr int N = 20;
    constexpr uintmax_t MAX_THRESHOLD = 200;

    std::filesystem::remove_all(DB_DIR);
    {
        Database db(MAX_THRESHOLD);
        std::uintmax_t tracked = std::filesystem::file_size(WAL_PATH);
        bool resetSeen = false;

        for (int i = 1; i <= N; ++i) {
            const auto before = std::filesystem::file_size(WAL_PATH);
            db.put("k" + std::to_string(i), "v" + std::to_string(i));
            const auto real = std::filesystem::file_size(WAL_PATH);

            std::cout << "put k" << i
                      << " | wal size: " << real
                      << " | prev: " << before << '\n';

            if (real < before) resetSeen = true;
        }

        if (!resetSeen) {
            std::cerr << "FAIL: checkpoint never fired\n";
            std::abort();
        }
    }
    {
        Database db(MAX_THRESHOLD);
        for (int i = 1; i <= N; ++i) {
            if (!db.has("k" + std::to_string(i))) {
                std::cerr << "FAIL: k" << i << " missing after restart\n";
                std::abort();
            }
        }
        db.put("k21", "v21");
    }
    {
        Database db(MAX_THRESHOLD);
        if (!db.has("k21")) {
            std::cerr << "FAIL: k21 lost (LSN restarted?)\n";
            std::abort();
        }
    }
    std::cout << "ok\n";
}

/// Simulate a crash before the wal reset (fn reset()) and after the snapshot save (fn saveToDisk).
void crashBeforeTheResetTest() {

    std::cout << "===crashBeforeTheResetTest===" << std::endl;
    std::filesystem::remove_all(DB_DIR);

    {
        Database db;
        db.put("k1", "v1");
        db.put("k2", "v2");
        db.put("k3", "v3");
        db.saveToDisk();
        db.put("k4", "v4");
        db.remove("k1");
    }
    {
        Database db;
        if (db.has("k1") || !db.has("k2") || !db.has("k3") || !db.has("k4")) {
            std::cerr << "FAIL: wrong state after restart\n";
            abort();
        }
        db.put("x", "x1");
    }
    {
        Database db;
        if (!db.has("x")) {
            std::cerr << "FAIL: x lost\n";
            abort();
        }
    }
    std::cout << "ok\n";
}
