#!/usr/bin/env bash
# Benchmarks MiniRedis against Redis with redis-benchmark and prints a Markdown table.
#
#   bench/run_benchmarks.sh [path/to/miniredis]
#
# Needs redis-server and redis-benchmark on PATH. Build MiniRedis in Release mode first:
#   cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DMINIREDIS_BUILD_TESTS=OFF
#   cmake --build build-release
set -euo pipefail

MINIREDIS=${1:-build-release/miniredis}
REQUESTS=${REQUESTS:-200000}
KEYSPACE=100000
REDIS_PORT=6400
MINIREDIS_PORT=6401
WORKDIR=$(mktemp -d)
PIDS=()

cleanup() {
    for pid in ${PIDS[@]+"${PIDS[@]}"}; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    rm -rf "$WORKDIR"
}
trap cleanup EXIT

wait_for_port() {
    for _ in $(seq 1 50); do
        redis-cli -p "$1" PING >/dev/null 2>&1 && return 0
        sleep 0.1
    done
    echo "server on port $1 did not start" >&2
    exit 1
}

start_redis() {
    redis-server --port "$REDIS_PORT" --save "" --dir "$WORKDIR" "$@" >"$WORKDIR/redis.log" 2>&1 &
    PIDS+=($!)
    wait_for_port "$REDIS_PORT"
}

# Each client holds a worker thread for its whole connection, so the pool must cover the clients.
start_miniredis() {
    "$MINIREDIS" --port "$MINIREDIS_PORT" --threads 128 "$@" >"$WORKDIR/miniredis.log" 2>&1 &
    PIDS+=($!)
    wait_for_port "$MINIREDIS_PORT"
}

stop_servers() {
    for pid in ${PIDS[@]+"${PIDS[@]}"}; do
        kill "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    PIDS=()
}

# Prints "<SET rps> <GET rps>".
measure() {
    local port=$1 clients=$2 pipeline=$3
    redis-benchmark -p "$port" -t set,get -n "$REQUESTS" -r "$KEYSPACE" -c "$clients" \
        -P "$pipeline" -q --csv 2>/dev/null |
        awk -F'","' '/^"SET"/ {set=$2} /^"GET"/ {get=$2} END {printf "%.0f %.0f\n", set, get}'
}

row() {
    local label=$1 clients=$2 pipeline=$3
    read -r redis_set redis_get <<<"$(measure "$REDIS_PORT" "$clients" "$pipeline")"
    read -r mini_set mini_get <<<"$(measure "$MINIREDIS_PORT" "$clients" "$pipeline")"
    printf '| %s | %s | %s | %s | %d%% | %s | %s | %d%% |\n' "$label" "$clients" \
        "$redis_set" "$mini_set" $((100 * mini_set / redis_set)) \
        "$redis_get" "$mini_get" $((100 * mini_get / redis_get))
}

header() {
    echo "| Scenario | Clients | Redis SET/s | MiniRedis SET/s | Ratio | Redis GET/s | MiniRedis GET/s | Ratio |"
    echo "|---|---:|---:|---:|---:|---:|---:|---:|"
}

echo "$(redis-server --version | cut -d' ' -f1-3), $REQUESTS requests per test, keyspace $KEYSPACE, $(uname -sm)"
echo
header

start_redis --appendonly no
start_miniredis
for clients in 1 10 50; do
    row "In memory" "$clients" 1
done
row "In memory, pipeline 16" 50 16
stop_servers

start_redis --appendonly yes --appendfsync everysec
start_miniredis --aof "$WORKDIR/miniredis.aof" --fsync everysec
row "AOF, fsync everysec" 50 1
stop_servers
