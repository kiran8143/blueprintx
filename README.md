# BlueprintX — Zero-Config C++20 REST Framework

> Point it at any database. Get a fully functional REST API. No code generation. No migrations. No boilerplate.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-orange.svg)](https://en.cppreference.com/w/cpp/20)
[![Drogon](https://img.shields.io/badge/Drogon-v1.9.12-green.svg)](https://github.com/drogonframework/drogon)
[![Performance](https://img.shields.io/badge/Throughput-32K%20RPS-red.svg)](#performance)

---

## What is BlueprintX?

BlueprintX is a **production-ready C++20 REST API framework** that introspects your database schema at startup and automatically serves every table as a fully-featured REST resource — with pagination, filtering, sorting, JWT auth, two-tier caching, rate limiting, and live OpenAPI docs.

**No ORMs. No code generation step. No configuration files per table.** Just point it at your database and go.

```bash
# One .env file. That's it.
DB_ENGINE=postgresql
DB_HOST=localhost
DB_NAME=myapp

# Start the server
./drogon-blueprint

# Every table in your database is now a REST API
curl http://localhost:8080/api/v1/users
curl http://localhost:8080/api/v1/products?limit=20&sort=created_at:desc
curl http://localhost:8080/api/v1/orders/42
```

---

## Performance

Benchmarked against Fastify (Node.js) and FastAPI (Python) on real Azure MySQL over a 40ms network RTT with 100,000 rows.

### Single Row by ID — `GET /api/v1/users/1`

| Concurrency | BlueprintX RPS | Fastify RPS | FastAPI RPS | BlueprintX p99 |
|:-----------:|---------------:|------------:|------------:|---------------:|
| 10          | **27,863**     | 131         | 138         | 1ms            |
| 50          | **24,376**     | 138         | 139         | 6ms            |
| 100         | **32,057**     | 137         | 130         | 5ms            |

### Large Result Sets — `GET /api/v1/users?limit=N`

| Result Size | BlueprintX RPS | vs Fastify   | vs FastAPI   | p98    |
|:-----------:|---------------:|:------------:|:------------:|-------:|
| 1 row       | **20,100**     | 574x faster  | 565x faster  | 3ms    |
| 100 rows    | **9,082**      | 259x faster  | 268x faster  | 13ms   |
| 1,000 rows  | **1,101**      | 36x faster   | 43x faster   | 98ms   |

> Zero failures across 25,000+ benchmark requests. Sub-10ms p99 at 100 concurrent connections.

### Why C++20 Wins Here

- **Multi-threaded coroutines** — 12 async threads vs Node.js/Python single-threaded event loop
- **Zero GC pauses** — predictable p99, no stop-the-world garbage collection
- **mimalloc** — high-performance memory allocator, 2–4x faster allocations
- **-O3 + LTO** — native binary with full link-time optimizations
- **Compiled JSON & HTTP parsing** — no interpreter overhead

---

## Features

### Zero-Config CRUD
Every table in your database becomes a REST resource automatically. No code to write per table.

```
GET    /api/v1/{table}           List with pagination, filtering, sorting
POST   /api/v1/{table}           Create a new record
POST   /api/v1/{table}/bulk      Bulk insert
GET    /api/v1/{table}/{id}      Get by primary key
PUT    /api/v1/{table}/{id}      Update a record
DELETE /api/v1/{table}/{id}      Delete a record
```

### Multi-Database Support
Switch databases with one env variable. No code changes.

| Database   | Engine Value  | Notes                              |
|:----------:|:-------------:|:-----------------------------------|
| PostgreSQL | `postgresql`  | Full support including JSONB        |
| MySQL 8    | `mysql`       | Native JSON column support         |
| SQLite 3   | `sqlite3`     | Great for development & embedded   |

### Two-Tier Cache

```
Request → L1 (in-process LRU, μs) → L2 (Redis, sub-ms) → Database
```

- Cache keys scoped per table + query params
- Automatic cache invalidation on write operations (POST/PUT/DELETE)
- Redis is optional — falls back to L1 only

### JWT Authentication

```bash
# Skip auth on public routes
curl http://localhost:8080/api/v1/health

# Protected routes require Bearer token
curl -H "Authorization: Bearer <token>" http://localhost:8080/api/v1/users
```

Configurable public route bypass. All other routes require a valid HS256 JWT.

### Data Protection

**FieldGuard** — mass-assignment protection strips dangerous fields from user input:
- Primary keys (`id`)
- Audit columns (`created_at`, `updated_at`, `created_by`, `modified_by`)
- Soft-delete markers (`deleted_at`, `deleted_by`)

**AuditInjector** — automatically injects audit trail timestamps on create/update if those columns exist in the table schema.

**CodeGenerator** — auto-generates UUID v4 for `code` columns on INSERT.

### OpenAPI Docs (Auto-Generated)

```
GET /api/docs              → Swagger UI
GET /api/docs/openapi.json → Raw OpenAPI 3.0 spec
```

The spec is generated from your live database schema. Every table, every column, every type — accurate and always up to date.

### Query API

```bash
# Pagination
GET /api/v1/users?limit=20&offset=40

# Filtering
GET /api/v1/orders?status=active&user_id=5

# Sorting
GET /api/v1/products?sort=price:asc

# Combined
GET /api/v1/posts?author_id=1&limit=10&sort=created_at:desc
```

### Rate Limiting

Per-IP sliding window rate limiter with `429 Too Many Requests` and `Retry-After` headers. Configurable max requests and window duration via `.env`.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     HTTP Request                        │
└───────────────────────────┬─────────────────────────────┘
                            ▼
┌───────────────────────────────────────────────────────────┐
│  Middleware Chain                                         │
│  CorsMiddleware → RateLimiter → JwtMiddleware             │
└───────────────────────────┬───────────────────────────────┘
                            ▼
┌───────────────────────────────────────────────────────────┐
│  BlueprintController  (wildcard route handler)            │
│  • Lookup table in ModelRegistry                         │
│  • Check CacheManager (L1 → L2)                          │
│  • RequestValidator (payload type checking)              │
│  • FieldGuard (mass-assignment protection)               │
│  • AuditInjector (timestamps)                            │
│  • QueryBuilder (parameterized, dialect-aware SQL)       │
│  • JsonSerializer (type-aware row → JSON)                │
│  • ApiResponse (standard envelope)                       │
└───────────────────────────┬───────────────────────────────┘
                            ▼
┌──────────────────┐    ┌───────────────────────────────────┐
│  CacheManager    │    │  Database                         │
│  L1: LruCache    │    │  PostgreSQL / MySQL / SQLite       │
│  L2: RedisCache  │    │  Async connection pool            │
└──────────────────┘    └───────────────────────────────────┘
```

**Schema Introspection** runs once at startup (async, after event loop boots) and populates the `ModelRegistry` — an in-memory, `shared_mutex`-protected map of every table's column metadata. All request handling is read-only against this registry.

### Source Layout

```
src/
├── api/            Response envelope builders (ok, created, paginated, error)
├── cache/          LruCache, RedisCache, CacheManager
├── config/         EnvConfig — 12-factor .env loader
├── controllers/    BlueprintController, HealthController
├── database/       DatabaseManager — pool registration & warming
├── middleware/     JwtMiddleware, CorsMiddleware, RateLimiter
├── openapi/        OpenApiGenerator — live schema → OpenAPI 3.0
├── protection/     FieldGuard, AuditInjector, CodeGenerator
├── query/          QueryBuilder — parameterized, multi-dialect SQL
└── schema/         Types, SchemaIntrospector (MySQL/Postgres/SQLite),
                    ModelRegistry, TypeMapper, JsonSerializer,
                    RequestValidator
```

---

## Quick Start

### Prerequisites

- GCC 11+ or Clang 14+ (C++20 coroutines required)
- CMake 3.20+
- [vcpkg](https://github.com/microsoft/vcpkg) for dependency management
- Bison + Flex (required by libpq on some distros: `sudo apt install bison flex`)

### 1. Clone & Install Dependencies

```bash
git clone https://github.com/kiran8143/blueprintx
cd blueprintx

# Install vcpkg if needed
git clone https://github.com/microsoft/vcpkg ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh

# Install project dependencies (Drogon, jwt-cpp, mimalloc)
vcpkg install
```

### 2. Configure

```bash
cp .env.example .env
```

Edit `.env` with your database credentials:

```env
DB_ENGINE=postgresql        # postgresql | mysql | sqlite3
DB_HOST=localhost
DB_NAME=myapp
DB_USER=postgres
DB_PASSWORD=secret
JWT_SECRET=your-secret-key
```

### 3. Build

```bash
# Debug build
cmake -S . -B build \
  -DCMAKE_TOOLCHAIN_FILE=$HOME/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build -j$(nproc)

# Release build (optimized, ~15MB binary)
./build-release.sh
```

### 4. Run

```bash
./build/drogon-blueprint
```

```
[INFO] BlueprintX starting on 0.0.0.0:8080
[INFO] DB engine: postgresql | pool: 5
[INFO] Redis L2 cache: enabled
[INFO] Introspecting schema...
[INFO] Registered 12 tables, 87 columns
[INFO] OpenAPI docs: http://localhost:8080/api/docs
[INFO] Server ready.
```

Every table in your database is now live.

---

## Configuration Reference

All configuration is via `.env` (see [.env.example](.env.example) for the full reference).

| Variable | Default | Description |
|:---------|:-------:|:------------|
| `PORT` | `8080` | Server port |
| `DB_ENGINE` | — | `postgresql`, `mysql`, or `sqlite3` |
| `DB_HOST` | — | Database host |
| `DB_NAME` | — | Database name |
| `DB_USER` | — | Database user |
| `DB_PASSWORD` | — | Database password |
| `DB_POOL_SIZE` | `5` | Connection pool size |
| `JWT_SECRET` | — | HS256 signing secret |
| `JWT_EXPIRY_SECONDS` | `3600` | Token lifetime |
| `REDIS_HOST` | — | Redis host (optional — disables L2 if empty) |
| `CORS_ORIGINS` | — | Comma-separated allowed origins |
| `CACHE_ENABLED` | `true` | Set to `false` to disable all caching |
| `LOG_LEVEL` | `INFO` | `TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR` |

---

## API Response Format

All responses use a consistent JSON envelope:

```json
// List (paginated)
{
  "data": [...],
  "meta": {
    "total": 1000,
    "limit": 20,
    "offset": 0,
    "has_more": true
  }
}

// Single resource
{
  "data": { "id": 1, "name": "Alice", "email": "alice@example.com" }
}

// Error
{
  "error": "Not Found",
  "message": "Table 'foobar' is not registered"
}
```

---

## Health Check

```bash
curl http://localhost:8080/health
```

```json
{
  "status": "ok",
  "databases": ["default"],
  "tables": 12,
  "uptime_seconds": 3600
}
```

---

## System Tuning (Production)

For maximum throughput on Linux:

```bash
sudo sysctl -w net.core.somaxconn=65535
sudo sysctl -w net.ipv4.tcp_max_syn_backlog=65535
sudo sysctl -w net.ipv4.tcp_tw_reuse=1
sudo sysctl -w net.ipv4.tcp_fin_timeout=15
sudo sysctl -w net.ipv4.ip_local_port_range="1024 65535"
```

---

## Dependencies

| Package | Purpose |
|:--------|:--------|
| [Drogon v1.9.12](https://github.com/drogonframework/drogon) | Async HTTP server, coroutine runtime, DB client |
| [jwt-cpp](https://github.com/Thalhammer/jwt-cpp) | JWT HS256 sign/verify |
| [mimalloc](https://github.com/microsoft/mimalloc) | High-performance memory allocator |
| [jsoncpp](https://github.com/open-source-parsers/jsoncpp) | JSON parsing & serialization (via Drogon) |

All managed via [vcpkg](https://github.com/microsoft/vcpkg) — see [vcpkg.json](vcpkg.json).

---

## License

MIT — see [LICENSE](LICENSE).

---

## Author

**Udaykiran Atta**
Built with C++20, Drogon, and an unreasonable dislike for slow APIs.
