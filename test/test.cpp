#include "../database/database.h"
#include <iostream>


namespace {
    const std::filesystem::path DB_DIR = "../dbfile";
    const std::filesystem::path WAL_DIR = DB_DIR / "walfiles";
    const std::filesystem::path WAL_PATH = WAL_DIR / "wal_000000.log";   // primo segmento
    const std::filesystem::path WAL_BACKUP = DB_DIR / "wal.backup";
}

/// Two owners use the same key name: their records must stay distinct
/// across snapshot, WAL replay and restart. Removing a key for one owner
/// must not touch the other owner's record.
void testOwnerIsolation() {
    std::cout << "=== testOwnerIsolation ===\n";
    constexpr std::uint64_t THRESHOLD = 1024ULL * 1024 * 1024;
    constexpr std::uint64_t SEG = 16 * 1024 * 1024;

    auto fail = [](const char* msg) {
        std::cerr << "FAIL: " << msg << '\n';
        std::abort();
    };

    std::filesystem::remove_all(DB_DIR);
    {
        Database db(THRESHOLD, SEG);
        db.put(1, "a", "x");
        db.put(2, "a", "y");
        db.saveToDisk();          // (1,a) and (2,a) in the snapshot

        db.put(1, "b", "z");      // WAL only
        db.put(2, "c", "w");      // WAL only
        db.remove(2, "a");        // WAL only: must not affect (1,a)

        // live checks (same process)
        if (db.get(1, "a") != "x") fail("live: (1,a) wrong");
        if (db.has(2, "a")) fail("live: (2,a) should be removed");
        if (db.has(2, "b")) fail("live: (2,b) must not exist");
    }   // simulated crash: no checkpoint

    {
        Database db(THRESHOLD, SEG);   // snapshot + replay
        if (db.get(1, "a") != "x") fail("restart: (1,a) wrong");
        if (db.get(1, "b") != "z") fail("restart: (1,b) wrong");
        if (db.get(2, "c") != "w") fail("restart: (2,c) wrong");
        if (db.has(2, "a")) fail("restart: (2,a) should stay removed");
        if (db.has(1, "c")) fail("restart: (1,c) leaked from owner 2");
        if (db.has(2, "b")) fail("restart: (2,b) leaked from owner 1");
        if (db.has(0, "a"))  fail("restart: owner 0 must see nothing");

        db.put(2, "a", "new");         // same key name again, other owner untouched
    }
    {
        Database db(THRESHOLD, SEG);
        if (db.get(2, "a") != "new") fail("second restart: (2,a) wrong");
        if (db.get(1, "a") != "x")   fail("second restart: (1,a) changed");
    }
    std::cout << "ok\n";
}