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
