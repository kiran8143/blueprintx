// Author: Udaykiran Atta
// License: MIT
//
// Fastify benchmark server — comparison server for BlueprintX benchmarks.
// Configure via environment variables (same as .env.example).

const fastify = require('fastify')({ logger: false });
const mysql = require('mysql2/promise');

const pool = mysql.createPool({
  host: process.env.DB_HOST || 'localhost',
  port: parseInt(process.env.DB_PORT || '3306'),
  user: process.env.DB_USER || 'root',
  password: process.env.DB_PASSWORD || '',
  database: process.env.DB_NAME || 'benchmark',
  ssl: process.env.DB_SSL === 'true' ? { rejectUnauthorized: false } : undefined,
  connectionLimit: parseInt(process.env.DB_POOL_SIZE || '5'),
  waitForConnections: true,
});

// GET /api/v1/users/:id
fastify.get('/api/v1/users/:id', async (req, reply) => {
  const [rows] = await pool.execute('SELECT * FROM `users` WHERE `id` = ?', [req.params.id]);
  if (rows.length === 0) {
    reply.code(404);
    return { error: { message: 'Not found', status: 404 } };
  }
  return { data: rows[0] };
});

// GET /api/v1/users
fastify.get('/api/v1/users', async (req, reply) => {
  const limit = parseInt(req.query.limit || '20');
  const offset = parseInt(req.query.offset || '0');
  const [[{ total }]] = await pool.execute('SELECT COUNT(*) as total FROM `users`');
  const [rows] = await pool.execute(
    'SELECT * FROM `users` ORDER BY `id` ASC LIMIT ? OFFSET ?',
    [limit, offset]
  );
  return { data: rows, meta: { total, limit, offset, has_more: offset + limit < total } };
});

// POST /api/v1/users
fastify.post('/api/v1/users', async (req, reply) => {
  reply.code(201);
  const body = req.body;
  const now = new Date().toISOString().slice(0, 19).replace('T', ' ');
  const code = require('crypto').randomUUID();
  const [result] = await pool.execute(
    'INSERT INTO `users` (`name`,`email`,`role`,`code`,`is_active`,`created_at`,`updated_at`,`created_by`,`modified_by`) VALUES (?,?,?,?,1,?,?,?,?)',
    [body.name, body.email, body.role || 'user', code, now, now, 'system', 'system']
  );
  const [rows] = await pool.execute('SELECT * FROM `users` WHERE `id` = ?', [result.insertId]);
  return { data: rows[0] };
});

const port = parseInt(process.env.PORT || '3001');
fastify.listen({ port, host: '0.0.0.0' }, (err) => {
  if (err) { console.error(err); process.exit(1); }
  console.log(`Fastify listening on port ${port}`);
});
