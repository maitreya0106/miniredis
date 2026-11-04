# Design decisions

Each entry records what was chosen, what it was weighed against, and why.

## 1. Fixed-size thread pool, one client per worker

**Chosen:** A pool of N worker threads (`--threads`, default 16). The accept loop hands each new connection to the pool, and a worker serves that client until it disconnects.

**Alternatives:**
- *Thread per client:* simplest, but the thread count is unbounded. Each thread reserves a stack (8 MB virtual by default on Linux), and a connection flood turns into a thread-creation storm.
- *Event loop (epoll/kqueue):* how Redis really works. One thread multiplexes thousands of sockets. It is explicitly out of scope.

**Trade-off:** The pool caps resource usage and avoids paying thread creation per connection. The cost is that **at most N clients are served at once**, because a worker blocks in `recv()` for its client's whole lifetime, even when the client is idle. Size `--threads` to the expected number of concurrent clients. The benchmarks run with `--threads` ≥ the client count.

## 2. Unbounded queue when all workers are busy

**Chosen:** Connections beyond N wait in the pool's task queue. They're accepted at the TCP level, but nothing reads from them until a worker frees up.

**Alternative:** A bounded queue that replies `-ERR max clients reached` and closes (Redis's `maxclients`).

**Why:** It's simpler, and the client sees a slow server rather than an error. The risk is that a queued client can wait indefinitely behind idle ones. A bounded queue with rejection is the natural next step.

## 3. A single mutex around the store

**Chosen:** One `std::mutex` guards the hash map. Every command holds it for the duration of one store operation. Multi-key `DEL`/`EXISTS` hold it for the whole batch, so they're atomic, as in Redis.

**Alternative:** Sharded locks: K maps, each with its own mutex, with the shard picked by `hash(key) % K`.

**Trade-off:**
- A single lock is trivially correct, and it makes multi-key operations and (later) LRU ordering and AOF ordering easy. The critical sections are tiny (a hash lookup), so the lock is held for nanoseconds while the request spends microseconds in syscalls.
- Sharding raises write throughput on many cores, but it makes global structures hard. One LRU list across all keys would need its own lock, multi-key commands would need ordered multi-lock acquisition to avoid deadlock, and the AOF would need a global order.
- A `shared_mutex` (readers/writer lock) doesn't help much here, because from Phase 2 on even `GET` writes: it moves the key to the front of the LRU list.

## 4. Inline (line-based) requests, RESP-framed replies

**Chosen:** Requests are whitespace-separated lines with redis-cli quoting rules. Replies use RESP framing (`+OK`, `:1`, `$5\r\nhello`, `$-1`).

**Why:** This is exactly what Redis does for "inline commands": it accepts the line format for humans using `nc`/`telnet` but always answers in RESP. Simple types stay on one line, so it's still readable by hand, and it meant Phase 4 only had to add a request parser, not touch the reply path. Parsers share one contract (`ParseResult`: incomplete / complete + bytes consumed / error), so they're interchangeable.

## 5. Shutdown: self-pipe and `shutdown()` on client sockets

`accept()` and `recv()` can't be interrupted portably from another thread. Closing a listening socket from another thread doesn't reliably wake `accept()` on macOS.

- The accept loop uses `poll()` on the listener plus a self-pipe, and `stop()` writes a byte to the pipe. (Plain `poll()` on two fds is portable POSIX. It's not an event-loop server.)
- `stop()` calls `shutdown(fd, SHUT_RDWR)` on every tracked client socket, which makes the blocked `recv()` return 0.
- A connection's fd is untracked *before* it's closed. Otherwise the kernel could reuse the number for a new client, and a stale untrack would hide that client from `stop()`.
- SIGINT/SIGTERM are blocked in all threads and received by one thread via `sigwait()`. That way the shutdown code runs in a normal thread, not in a signal handler where almost nothing is async-signal-safe.

## 6. SIGPIPE

Writing to a socket the peer has closed raises SIGPIPE, which kills the process by default. The server uses `MSG_NOSIGNAL` on Linux and `SO_NOSIGPIPE` on macOS, which has no `MSG_NOSIGNAL`. It doesn't change the process-wide signal disposition, so the networking code stays library-safe.

## 7. Expiry: lazy check plus a background sweep (the Redis way)

**Chosen:** Every key may carry an absolute deadline.
- *Lazy:* each access (`GET`, `EXISTS`, `TTL`, `DEL`, `SET`, `EXPIRE`) first checks the deadline. An expired key is deleted then and there and treated as missing. This alone guarantees correctness: no client ever sees an expired value.
- *Active:* lazy expiry alone leaks memory for keys that are never read again. An `ExpirySweeper` thread wakes 10 times a second (Redis's default `hz`) and runs Redis's adaptive algorithm. It samples 20 random keys *from the set of keys that have a TTL*, deletes the expired ones, and repeats while more than 25% of the sample had expired, within a 2 ms budget per cycle.

**Alternatives:**
- *Only lazy:* unbounded memory growth from write-once keys.
- *Only active, scanning everything:* O(n) work per cycle, and it holds the lock long enough to stall clients.
- *A min-heap or timer wheel ordered by deadline:* exact and prompt, but an extra O(log n) structure to keep in sync on every TTL change. Random sampling is statistically good enough. It bounds the fraction of expired-but-unreclaimed keys to roughly 25%, at constant cost.

**Data structure:** `volatile_` is a `std::vector<Entry*>` of keys with a TTL. Sampling is a random index (O(1)). Removal swaps the entry with the last element and pops, which is O(1), and each entry stores its own index so it can be found.

**Lock granularity:** the store lock is released between 20-key samples, so a sweep never blocks clients for long.

## 8. Wall-clock deadlines, not `steady_clock`

Deadlines are stored as `system_clock` milliseconds since the epoch. A monotonic clock would be immune to clock jumps, but its epoch is arbitrary (often boot time), so a deadline written to the AOF would be meaningless after a restart. Redis makes the same choice: it persists absolute Unix-time deadlines (`PEXPIREAT`). The clock is injectable (`StoreOptions::now`), so tests control time deterministically with no sleeps.

## 9. LRU eviction: hash map + hand-written intrusive doubly linked list

**Chosen:** With `--maxkeys N`, inserting a new key when the store holds N keys first evicts the least recently used one. `GET`, `SET` and `EXPIRE` count as a use. `EXISTS` and `TTL` don't, because they inspect without using.

- The list is **intrusive**: `lru_prev`/`lru_next` live inside each map entry, so there is no second allocation per key and no `new`/`delete`. Touch, insert and evict are O(1) pointer swaps.
- This relies on a guarantee of `std::unordered_map`: **rehashing invalidates iterators but not references or pointers to elements**. That's what makes it safe for the list (and `volatile_`) to hold plain `Entry*`.
- Each entry keeps a pointer to its own key (the map's key, also stable), so the evicted tail can be erased from the map without searching.

**Alternatives:**
- `std::list` plus a map of iterators: the textbook version, but two allocations per key and the brief asked for a hand-written list.
- *Approximate LRU* (Redis samples 5 keys and evicts the oldest): no list at all, less memory, not exact.

**Why reads take the exclusive lock:** a `GET` moves the key to the front of the list, which is a write. That's the main reason a `shared_mutex` wouldn't help (see entry 3).

## 10. Append-only file: what is logged, and when

**Chosen:** The store calls a `WriteLog` interface for every mutation *while holding its lock, before applying the change*. `AofWriter` implements it.

- **Same lock, same order.** If logging happened outside the store lock, two concurrent `SET k` could be applied A→B but logged B→A, and a replay would restore the wrong value. Logging inside the critical section makes the file order equal the memory order.
- **Log before apply.** A crash between the two means the write is in the log but the client never got a reply. Replaying it is harmless, because the client didn't see success either way. The opposite order could acknowledge a write that was never logged.
- **Effects, not requests.** Commands are rewritten into deterministic forms:
  - `SET k v EX 10` → `SET k v PXAT <unix-ms>`, and `EXPIRE` → `PEXPIREAT`. A replay an hour later mustn't restart a 10-second countdown.
  - Lazy and active expirations and LRU evictions are logged as `DEL`. Replay then never depends on the clock or on the read pattern that shaped the LRU order (reads aren't logged).
  - An eviction's `DEL` is logged *before* the `SET` that triggered it, so the replay never needs to pick a victim itself.
- Replay runs commands through the normal `CommandDispatcher`, with the write log detached so replayed commands aren't appended again.

## 11. AOF format: RESP

The file is a sequence of RESP arrays, the same format Redis uses and the same parser the network uses. Records are length-prefixed and binary-safe: values can contain spaces, newlines or NUL bytes.

**Torn writes:** a crash or power loss can leave a partial final record. On startup, an *incomplete* record at the end is truncated with a warning. A *malformed* record anywhere is real corruption, and the server refuses to start, matching Redis's `aof-load-truncated yes` behaviour.

## 12. fsync policy: `always` vs `everysec`

| | `always` | `everysec` (default) |
|---|---|---|
| When data reaches the disk | before the reply is sent | within ~1 s, from a background thread |
| Lost on power failure / kernel panic | nothing acknowledged | up to ~1 s of writes |
| Lost on process crash (`kill -9`) | nothing | **nothing**: `write()` already put it in the kernel page cache |
| Cost | one disk flush per write, under the store lock, so it serializes all clients | almost free |

The last row is the subtle one. fsync protects against the *machine* dying, not the *process*. The crash test kills the server with SIGKILL under both policies and checks that every acknowledged write survives.

Platform details:
- On macOS, `fsync()` only pushes data to the drive's volatile cache, so the writer uses `fcntl(F_FULLFSYNC)`. That's why `always` is far slower on a Mac.
- On Linux, `fdatasync()` skips non-essential metadata.
- When the AOF is first created, the parent directory is fsynced as well, or the file itself could vanish after a power loss.

**Write failures fail stop.** If `write()` or `fsync()` fails, the server aborts. Continuing could leave a hole in the middle of the log, or acknowledge writes that aren't durable. Dying leaves at worst a torn tail, which startup repairs.

**Not built:** AOF rewriting/compaction (`BGREWRITEAOF`). The file grows forever, and replay time grows with it.

## 13. RESP on the wire, detected per request

**Chosen:** Requests that start with `*` are parsed as RESP arrays of bulk strings, and anything else as inline commands. The choice is made per request, as in Redis, so `redis-cli`, client libraries and `nc` all share one port. Replies were RESP from the start (entry 4), so Phase 4 only added a parser and touched nothing downstream.

- Only the subset clients send is implemented: arrays of bulk strings. Replies use simple strings, errors, integers, bulk strings, nil and flat arrays (RESP2). `HELLO`/RESP3 aren't implemented. `redis-cli` uses RESP2 by default.
- **Length limits are checked before allocating**: at most 1M array elements, 512 MB per bulk string, and 64-byte header lines. A hostile `*999999999\r\n` therefore gets a protocol error instead of a huge `reserve()`.
- `CONFIG GET` returns an empty list, which is what Redis returns for an unknown parameter. That's only so `redis-benchmark` stops warning that it can't read the server config.
- A malformed request gets an error reply and the connection is closed. After a framing error there's no reliable way to find where the next request starts.

## 14. Benchmark methodology

Both servers run on the same machine, with persistence off (except in the AOF row), using the same random keyspace (`-r 100000`). Each client needs its own worker thread, so MiniRedis runs with `--threads 128` (entry 1). The comparison is honest rather than flattering. Redis's single-threaded event loop beats a lock-and-thread-per-client design once there's more than one client. The 1-client and pipelined rows show where the time actually goes: round trips and syscalls, not the hash map.

## 15. Compiler hardening

All builds link a `miniredis_hardening` target:
- `-fstack-protector-strong` and `-fstack-clash-protection`, each enabled only if the compiler accepts it. Apple Clang lacks the second.
- `_FORTIFY_SOURCE=3` in optimised, non-sanitizer builds. It needs `-O`, and it's undefined first because Ubuntu's GCC predefines it.
- Bounds-checked standard containers: `_LIBCPP_HARDENING_MODE_FAST` for libc++ and `_GLIBCXX_ASSERTIONS` for libstdc++. An out-of-range `operator[]` then traps instead of silently reading memory.

The runtime cost is around 1%, well inside benchmark noise. Functions whose result must not be ignored (the parsers, `send_all`, the socket factories, `load_aof`) are `[[nodiscard]]`.
