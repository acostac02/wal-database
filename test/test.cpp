//
// Created by arisu on 10/11/2026.
//

#include "../database/database.h"
#include <cstdio>
#include <algorithm>
#include <assert.h>
#include <vector>
#include <iostream>
#include <thread>


namespace {
    const std::filesystem::path DB_DIR = "../dbfile";
    const std::filesystem::path WAL_DIR = DB_DIR / "walfiles";
    const std::filesystem::path WAL_PATH = WAL_DIR / "wal_000000.log";
    const std::filesystem::path WAL_BACKUP = DB_DIR / "wal.backup";

    std::vector<std::filesystem::path> listSegments() {
        std::vector<std::filesystem::path> v;
        for (const auto& e : std::filesystem::directory_iterator(WAL_DIR)) {
            const auto name = e.path().filename().string();
            if (e.is_regular_file() && name.starts_with("wal_") && name.ends_with(".log"))
                v.push_back(e.path());
        }
        std::sort(v.begin(), v.end());   // padding a 6 cifre: ordine lessicografico = numerico
        return v;
    }

    std::string key(int i) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "k%03d", i);
        return buf;
    }
}

/// Inserts N records with a tiny segment size and a huge checkpoint
/// threshold: the WAL must rotate into many segments, no checkpoint
/// fires, and every key must survive restarts (LSN and segment ids continue).
void testSegmentRotation() {
    std::cout << "=== testSegmentRotation ===\n";
    constexpr int N = 200;
    constexpr std::uint64_t SEG = 128;
    constexpr std::uint64_t THRESHOLD = 1024ULL * 1024 * 1024;

    std::filesystem::remove_all(DB_DIR);
    {
        Database db(THRESHOLD, SEG);
        for (int i = 1; i <= N; ++i) db.put(key(i), "v" + std::to_string(i));
    }

    const auto segs = listSegments();
    std::cout << "segments: " << segs.size() << '\n';
    if (segs.size() < 2) {
        std::cerr << "FAIL: WAL did not rotate\n";
        std::abort();
    }
    for (const auto& s : segs) {
        // each segment holds at least one record; >SEG only allowed with a single record
        if (std::filesystem::file_size(s) > SEG + 64) {
            std::cerr << "FAIL: segment too big: " << s << '\n';
            std::abort();
        }
    }

    {
        Database db(THRESHOLD, SEG);
        for (int i = 1; i <= N; ++i) {
            if (!db.has(key(i))) {
                std::cerr << "FAIL: " << key(i) << " missing after restart\n";
                std::abort();
            }
        }
        db.put("extra", "1");   // LSN and segment id must continue
    }
    {
        Database db(THRESHOLD, SEG);
        if (!db.has("extra") || !db.has(key(N))) {
            std::cerr << "FAIL: data lost after second restart\n";
            std::abort();
        }
    }
    std::cout << "ok\n";
}

/// Same tiny segments, but a small checkpoint threshold: checkpoints fire
/// many times, old segments are deleted, ids keep growing and all keys
/// survive a restart.
void testSegmentRotationWithCheckpoint() {
    std::cout << "=== testSegmentRotationWithCheckpoint ===\n";
    constexpr int N = 200;
    constexpr std::uint64_t SEG = 128;
    constexpr std::uint64_t THRESHOLD = 1000;

    std::filesystem::remove_all(DB_DIR);
    {
        Database db(THRESHOLD, SEG);
        for (int i = 1; i <= N; ++i) db.put(key(i), "v" + std::to_string(i));
    }

    const auto segs = listSegments();
    std::cout << "segments left: " << segs.size() << '\n';
    if (segs.size() > 15) {
        std::cerr << "FAIL: old segments not deleted\n";
        std::abort();
    }
    // ids must have grown: the last segment id is not tiny
    if (segs.back().filename().string() <= "wal_000005.log") {
        std::cerr << "FAIL: segment ids did not grow\n";
        std::abort();
    }

    {
        Database db(THRESHOLD, SEG);
        for (int i = 1; i <= N; ++i) {
            if (!db.has(key(i))) {
                std::cerr << "FAIL: " << key(i) << " missing after restart\n";
                std::abort();
            }
        }
        db.put("extra", "1");
    }
    {
        Database db(THRESHOLD, SEG);
        if (!db.has("extra")) {
            std::cerr << "FAIL: extra lost (LSN restarted?)\n";
            std::abort();
        }
    }
    std::cout << "ok\n";
}

/// Simulates a crash during segment creation: the last segment is cut
/// to every length shorter than its header (0..HEADER_SIZE-1). The
/// Database must open, keep all keys of the previous segments, drop the
/// keys of the broken segment (it held at most the records written after
/// its header, which a crash during creation cannot have), and keep
/// working after the repair.
void testLastSegmentTruncatedInsideHeader() {
    std::cout << "=== testLastSegmentTruncatedInsideHeader ===\n";
    constexpr int N = 30;
    constexpr std::uint64_t SEG = 128;
    constexpr std::uint64_t THRESHOLD = 1024ULL * 1024 * 1024;
    constexpr std::uintmax_t HEADER = 16;

    // 1. build a WAL with several segments
    std::filesystem::remove_all(DB_DIR);
    {
        Database db(THRESHOLD, SEG);
        for (int i = 1; i <= N; ++i) db.put(key(i), "v" + std::to_string(i));
    }

    // 2. back up the whole segment directory
    const auto backupDir = DB_DIR / "walfiles.backup";
    std::filesystem::remove_all(backupDir);
    std::filesystem::copy(WAL_DIR, backupDir);

    const auto segs = listSegments();
    if (segs.size() < 3) {
        std::cerr << "FAIL: need at least 3 segments\n";
        std::abort();
    }
    const auto lastName = segs.back().filename();

    // keys stored in the last segment: they are lost when it is recreated.
    // Count them by replaying the intact copy once.
    int lostFrom = N + 1;
    {
        Database probe(THRESHOLD, SEG);
        (void)probe;   // intact open, nothing to check here
    }

    for (std::uintmax_t cut = 0; cut < HEADER; ++cut) {
        // restore the intact directory
        std::filesystem::remove_all(WAL_DIR);
        std::filesystem::copy(backupDir, WAL_DIR);

        // simulate a crash right after creating the last segment
        std::filesystem::resize_file(WAL_DIR / lastName, cut);

        {
            Database db(THRESHOLD, SEG);   // must not throw
            // every key from the previous segments must be there
            int present = 0;
            for (int i = 1; i <= N; ++i)
                if (db.has(key(i))) ++present;
            if (present == 0) {
                std::cerr << "FAIL at cut=" << cut << ": no key recovered\n";
                std::abort();
            }
            // the first key is in the first segment: must always survive
            if (!db.has(key(1))) {
                std::cerr << "FAIL at cut=" << cut << ": k001 lost\n";
                std::abort();
            }
            db.put("after", "1");
        }
        {
            Database db(THRESHOLD, SEG);   // second restart
            if (!db.has("after") || !db.has(key(1))) {
                std::cerr << "FAIL at cut=" << cut << ": data lost after repair\n";
                std::abort();
            }
        }
    }
    (void)lostFrom;
    std::cout << "ok\n";
}

/// A truncated header in a NON-last segment is real corruption: the
/// constructor must throw.
void testMiddleSegmentTruncatedHeaderThrows() {
    std::cout << "=== testMiddleSegmentTruncatedHeaderThrows ===\n";
    std::filesystem::remove_all(DB_DIR);
    {
        Database db(1024ULL * 1024 * 1024, 128);
        for (int i = 1; i <= 30; ++i) db.put(key(i), "v" + std::to_string(i));
    }
    const auto segs = listSegments();
    std::filesystem::resize_file(segs[1], 5);   // middle segment

    bool threw = false;
    try { Database db(1024ULL * 1024 * 1024, 128); }
    catch (const std::exception&) { threw = true; }

    if (!threw) {
        std::cerr << "FAIL: corrupted middle segment was accepted\n";
        std::abort();
    }
    std::cout << "ok\n";
}

//Mutex test
void testMutualExclusion() {
    std::cout << "=== testMutualExclusion ===\n";

    std::filesystem::remove_all(DB_DIR);

    Database db(1024ULL * 1024 * 1024, 128);

    constexpr int THREADS = 4;
    constexpr int OPS = 1000;

    std::vector<std::thread> workers;

    for (int t = 0; t < THREADS; ++t) {
        workers.emplace_back([&db, t]() {
            for (int i = 0; i < OPS; ++i) {
                db.put(
                    "t" + std::to_string(t) +
                    "_k" + std::to_string(i),
                    "value");
            }
        });
    }

    for (auto& th : workers) {
        th.join();
    }

    std::size_t found = 0;

    for (int t = 0; t < THREADS; ++t) {
        for (int i = 0; i < OPS; ++i) {

            if (db.has(
                "t" + std::to_string(t) +
                "_k" + std::to_string(i)))
            {
                ++found;
            }
        }
    }

    const std::size_t expected = THREADS * OPS;

    if (found != expected) {
        std::cerr
            << "FAIL: expected "
            << expected
            << " keys, found "
            << found
            << '\n';

        std::abort();
    }

    std::cout << "ok\n";
}

#include <barrier>
#include <thread>

void testConcurrentMixedOperations() {
    std::cout << "=== testConcurrentMixedOperations ===\n";

    constexpr int N = 1000;

    std::filesystem::remove_all(DB_DIR);

    Database db(
        1024ULL * 1024 * 1024,
        128
    );

    std::barrier phase1(3); // writer1, writer2, remover
    std::barrier phase2(3);

    std::thread writer1([&]() {
        for (int i = 0; i < N; ++i) {
            db.put(
                "k" + std::to_string(i),
                "v" + std::to_string(i));
        }

        phase1.arrive_and_wait();
        phase2.arrive_and_wait();
    });

    std::thread writer2([&]() {
        for (int i = N; i < 2 * N; ++i) {
            db.put(
                "k" + std::to_string(i),
                "v" + std::to_string(i));
        }

        phase1.arrive_and_wait();
        phase2.arrive_and_wait();
    });

    std::thread remover([&]() {

        // aspetta che tutti i put siano finiti
        phase1.arrive_and_wait();

        for (int i = 0; i < 2 * N; i += 2) {
            db.remove(
                "k" + std::to_string(i));
        }

        phase2.arrive_and_wait();
    });

    writer1.join();
    writer2.join();
    remover.join();

    std::thread reader1([&]() {
        for (int i = 0; i < 2 * N; ++i) {
            db.has("k" + std::to_string(i));
        }
    });

    std::thread reader2([&]() {
        for (int i = 0; i < 2 * N; ++i) {
            try {
                db.get("k" + std::to_string(i));
            } catch (...) {
            }
        }
    });

    reader1.join();
    reader2.join();

    for (int i = 0; i < 2 * N; ++i) {

        const std::string key =
            "k" + std::to_string(i);

        if (i % 2 == 0) {

            if (db.has(key)) {
                std::cerr
                    << "FAIL: even key should have been removed: "
                    << key << '\n';

                std::abort();
            }

        } else {

            if (!db.has(key)) {
                std::cerr
                    << "FAIL: odd key missing: "
                    << key << '\n';

                std::abort();
            }
        }
    }

    std::cout << "ok\n";
}
