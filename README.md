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

## Roadmap

- Atomic checkpoints (write to temp file, fsync, rename)
- WAL truncation at the last valid record
- Concurrency (thread-safe access)
- Key-value server with HTTP API

## On-disk format

All integers are little-endian (fixed width). Strings are
[length: uint32][bytes], without terminator.

Snapshot (dbfile.db):
  magic "KVDB" | version u32 | checkpoint_lsn u64 | count u64 | pairs...

WAL (wal.log):
  magic "KVWL" | version u32 | records...
  record: len u32 | crc32 u32 | payload (lsn u64 | op u8 | key | value)