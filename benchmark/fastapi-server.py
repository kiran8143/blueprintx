# Author: Udaykiran Atta
# License: MIT
#
# FastAPI benchmark server — comparison server for BlueprintX benchmarks.
# Configure via environment variables (same as .env.example).

import os
import uuid
from datetime import datetime
from contextlib import asynccontextmanager

import uvicorn
from fastapi import FastAPI, Query, HTTPException
from fastapi.responses import JSONResponse
import aiomysql

pool = None

@asynccontextmanager
async def lifespan(app):
    global pool
    pool = await aiomysql.create_pool(
        host=os.environ.get('DB_HOST', 'localhost'),
        port=int(os.environ.get('DB_PORT', '3306')),
        user=os.environ.get('DB_USER', 'root'),
        password=os.environ.get('DB_PASSWORD', ''),
        db=os.environ.get('DB_NAME', 'benchmark'),
        minsize=int(os.environ.get('DB_POOL_SIZE', '5')),
        maxsize=int(os.environ.get('DB_POOL_SIZE', '5')),
        autocommit=True,
        ssl=None if os.environ.get('DB_SSL') != 'true' else {'verify_cert': False},
    )
    print("FastAPI connected to MySQL")
    yield
    pool.close()
    await pool.wait_closed()

app = FastAPI(lifespan=lifespan)

def row_to_dict(row, cursor):
    cols = [d[0] for d in cursor.description]
    result = {}
    for col, val in zip(cols, row):
        if isinstance(val, datetime):
            result[col] = val.strftime('%Y-%m-%d %H:%M:%S')
        else:
            result[col] = val
    return result

@app.get("/api/v1/users/{user_id}")
async def get_user(user_id: int):
    async with pool.acquire() as conn:
        async with conn.cursor() as cur:
            await cur.execute("SELECT * FROM `users` WHERE `id` = %s", (user_id,))
            row = await cur.fetchone()
            if not row:
                raise HTTPException(status_code=404, detail="Not found")
            return row_to_dict(row, cur)

@app.get("/api/v1/users")
async def list_users(limit: int = Query(20, ge=1, le=50000), offset: int = Query(0, ge=0)):
    async with pool.acquire() as conn:
        async with conn.cursor() as cur:
            await cur.execute("SELECT COUNT(*) FROM `users`")
            (total,) = await cur.fetchone()
            await cur.execute("SELECT * FROM `users` ORDER BY `id` ASC LIMIT %s OFFSET %s", (limit, offset))
            rows = await cur.fetchall()
            data = [row_to_dict(r, cur) for r in rows]
            return {
                "data": data,
                "meta": {"total": total, "limit": limit, "offset": offset, "has_more": offset + limit < total},
            }

@app.post("/api/v1/users", status_code=201)
async def create_user(body: dict):
    now = datetime.utcnow().strftime('%Y-%m-%d %H:%M:%S')
    code = str(uuid.uuid4())
    async with pool.acquire() as conn:
        async with conn.cursor() as cur:
            await cur.execute(
                "INSERT INTO `users` (`name`,`email`,`role`,`code`,`is_active`,`created_at`,`updated_at`,`created_by`,`modified_by`) VALUES (%s,%s,%s,%s,1,%s,%s,%s,%s)",
                (body["name"], body["email"], body.get("role", "user"), code, now, now, "system", "system"),
            )
            last_id = cur.lastrowid
            await cur.execute("SELECT * FROM `users` WHERE `id` = %s", (last_id,))
            row = await cur.fetchone()
            return row_to_dict(row, cur)

if __name__ == "__main__":
    port = int(os.environ.get("PORT", "3002"))
    uvicorn.run(app, host="0.0.0.0", port=port, log_level="error")
