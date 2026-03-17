#!/usr/bin/env bash
set -euo pipefail

# ─── Configuration ───────────────────────────────────────────────
DROGON_PORT=8080
FASTIFY_PORT=3001
FASTAPI_PORT=3002
RESULTS_FILE="benchmark-results.txt"

# Concurrency levels and request counts
declare -A TESTS=(
  ["single_row_c10"]="n=2000,c=10,path=/api/v1/users/1"
  ["single_row_c50"]="n=3000,c=50,path=/api/v1/users/1"
  ["single_row_c100"]="n=5000,c=100,path=/api/v1/users/1"
  ["10_rows_c50"]="n=2000,c=50,path=/api/v1/users?limit=10"
  ["100_rows_c10"]="n=1000,c=10,path=/api/v1/users?limit=100"
  ["100_rows_c50"]="n=2000,c=50,path=/api/v1/users?limit=100"
  ["1000_rows_c10"]="n=500,c=10,path=/api/v1/users?limit=1000"
  ["10000_rows_c5"]="n=50,c=5,path=/api/v1/users?limit=10000"
)

# ─── Helpers ─────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; CYAN='\033[0;36m'; NC='\033[0m'

log()  { echo -e "${CYAN}[BENCH]${NC} $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
ok()   { echo -e "${GREEN}[OK]${NC} $*"; }
fail() { echo -e "${RED}[FAIL]${NC} $*"; }

wait_for_port() {
  local port=$1 name=$2 max=30
  for i in $(seq 1 $max); do
    if curl -s -o /dev/null --max-time 2 "http://127.0.0.1:${port}/api/v1/users/1" 2>/dev/null; then
      ok "$name ready on port $port"
      return 0
    fi
    sleep 1
  done
  fail "$name failed to start on port $port"
  return 1
}

run_ab() {
  local url=$1 n=$2 c=$3 label=$4
  local result
  result=$(ab -n "$n" -c "$c" "$url" 2>&1)

  local rps=$(echo "$result" | grep "Requests per second" | awk '{print $4}')
  local mean=$(echo "$result" | grep "Time per request" | head -1 | awk '{print $4}')
  local failed=$(echo "$result" | grep "Failed requests" | awk '{print $3}')
  local p50=$(echo "$result" | grep "  50%" | awk '{print $2}')
  local p95=$(echo "$result" | grep "  95%" | awk '{print $2}')
  local p99=$(echo "$result" | grep "  99%" | awk '{print $2}')
  local p100=$(echo "$result" | grep " 100%" | awk '{print $2}')

  printf "  %-12s | %8s RPS | mean=%5sms | p50=%4sms p95=%4sms p99=%4sms max=%4sms | failed=%s\n" \
    "$label" "$rps" "$mean" "$p50" "$p95" "$p99" "$p100" "$failed"

  # Also write to results file
  printf "%-30s | %-12s | %8s RPS | mean=%5sms | p50=%4sms p95=%4sms p99=%4sms max=%4sms | failed=%s\n" \
    "$label" "$4" "$rps" "$mean" "$p50" "$p95" "$p99" "$p100" "$failed" >> "$RESULTS_FILE"
}

# ─── Parse test config ───────────────────────────────────────────
parse_test() {
  local config=$1
  N=$(echo "$config" | grep -oP 'n=\K[0-9]+')
  C=$(echo "$config" | grep -oP 'c=\K[0-9]+')
  PATH_PART=$(echo "$config" | grep -oP 'path=\K.*')
}

# ─── Main ────────────────────────────────────────────────────────
main() {
  echo ""
  echo "============================================================"
  echo "  FRAMEWORK BENCHMARK: Drogon vs Fastify vs FastAPI"
  echo "  Database: Azure MySQL (100k users)"
  echo "  Date: $(date '+%Y-%m-%d %H:%M:%S')"
  echo "============================================================"
  echo ""

  > "$RESULTS_FILE"
  echo "# Framework Benchmark Results — $(date)" >> "$RESULTS_FILE"
  echo "" >> "$RESULTS_FILE"

  # Check which servers are running
  local servers=()
  if curl -s -o /dev/null --max-time 3 "http://127.0.0.1:${DROGON_PORT}/api/v1/users/1" 2>/dev/null; then
    ok "Drogon (C++) running on :${DROGON_PORT}"
    servers+=("drogon:${DROGON_PORT}")
  else
    warn "Drogon not running on :${DROGON_PORT}"
  fi

  if curl -s -o /dev/null --max-time 3 "http://127.0.0.1:${FASTIFY_PORT}/api/v1/users/1" 2>/dev/null; then
    ok "Fastify (Node.js) running on :${FASTIFY_PORT}"
    servers+=("fastify:${FASTIFY_PORT}")
  else
    warn "Fastify not running on :${FASTIFY_PORT}"
  fi

  if curl -s -o /dev/null --max-time 3 "http://127.0.0.1:${FASTAPI_PORT}/api/v1/users/1" 2>/dev/null; then
    ok "FastAPI (Python) running on :${FASTAPI_PORT}"
    servers+=("fastapi:${FASTAPI_PORT}")
  else
    warn "FastAPI not running on :${FASTAPI_PORT}"
  fi

  if [ ${#servers[@]} -eq 0 ]; then
    fail "No servers running. Start at least one server first."
    echo ""
    echo "  Drogon:  cd $(pwd) && ./build/drogon-blueprint"
    echo "  Fastify: cd benchmark && npm install && PORT=3001 node fastify-server.js"
    echo "  FastAPI: cd benchmark && pip install fastapi uvicorn aiomysql && PORT=3002 python fastapi-server.py"
    exit 1
  fi

  echo ""

  # Warmup
  log "Warming up servers..."
  for entry in "${servers[@]}"; do
    local name=${entry%%:*}
    local port=${entry##*:}
    for i in $(seq 1 20); do
      curl -s -o /dev/null "http://127.0.0.1:${port}/api/v1/users/1" &
    done
  done
  wait
  sleep 1
  ok "Warmup complete"
  echo ""

  # Run sorted tests
  local sorted_tests=(
    "single_row_c10"
    "single_row_c50"
    "single_row_c100"
    "10_rows_c50"
    "100_rows_c10"
    "100_rows_c50"
    "1000_rows_c10"
    "10000_rows_c5"
  )

  for test_name in "${sorted_tests[@]}"; do
    local config="${TESTS[$test_name]}"
    parse_test "$config"

    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    log "Test: ${test_name} (n=${N}, c=${C})"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "" >> "$RESULTS_FILE"
    echo "## ${test_name} (n=${N}, c=${C})" >> "$RESULTS_FILE"

    for entry in "${servers[@]}"; do
      local name=${entry%%:*}
      local port=${entry##*:}
      local url="http://127.0.0.1:${port}${PATH_PART}"
      run_ab "$url" "$N" "$C" "$name"
      sleep 0.5
    done
    echo ""
  done

  echo "============================================================"
  echo "  RESULTS SAVED TO: ${RESULTS_FILE}"
  echo "============================================================"
}

main "$@"
