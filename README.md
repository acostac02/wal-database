## Introduction

A key-value database written in C++, with durability provided by a
write-ahead log (WAL).

The WAL is an append-only file where every operation (put, remove) is
recorded *before* it is applied to the in-memory data. If the process
crashes, the database is rebuilt at startup from the last snapshot plus
the operations recorded in the WAL.

## Current features
- In-memory store with `put`, `get`, `remove`
- Binary snapshot file with a checkpoint LSN
- Binary WAL with per-record CRC-32 and replay on startup
- Durable WAL appends: each record is fsynced before `put`/`remove` returns
- Atomic snapshots: written to `dbfile.db.tmp`, fsynced, then atomically
  renamed over `dbfile.db` (the old snapshot stays valid until the swap)
- Automatic checkpoint: when the WAL reaches a configurable size (default 4 MB), 
  a snapshot is written atomically and the WAL is then truncated to its header.
- WAL segmentation with automatic segment rotation


## Roadmap
- Keep the WAL file opened between appends
- Bit-flip corruption test
- Concurrency (thread-safe access)
- Key-value server with HTTP API
- Authentication and per-user access control
- Possibility to add more data-type saving (json, images...)

## On-disk format

All integers are little-endian (fixed width). Strings are
[length: uint32][bytes], without terminator.

Snapshot (dbfile.db):
  magic "KVDB" | version u32 | checkpoint_lsn u64 | count u64 | pairs...

WAL (wal.log):
  magic "KVWL" | version u32 | records...
  record: len u32 | crc32 u32 | payload (lsn u64 | op u8 | key | value)