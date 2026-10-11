// Vercel serverless function: checks the sign-in form and sets the session cookie.
// Functions always receive the POST body (middleware may not), so the check lives here.
// The cookie value must match sessionToken() in middleware.js.
const crypto = require('crypto');

const USER = process.env.VIEWER_USER || 'Bigetrontemp';
const PASS = process.env.VIEWER_PASS || 'temp';
const SECRET = process.env.VIEWER_SECRET || 'bigetron-rail-sensor';
const MAX_AGE = 60 * 60 * 12;

const token = () => crypto.createHash('sha256').update(`${USER}\n${PASS}\n${SECRET}`).digest('hex');
const same = (a, b) => a.length === b.length && crypto.timingSafeEqual(Buffer.from(a), Buffer.from(b));

async function readForm(req) {
  if (req.body && typeof req.body === 'object' && !Buffer.isBuffer(req.body)) return req.body;
  if (typeof req.body === 'string' || Buffer.isBuffer(req.body)) return Object.fromEntries(new URLSearchParams(String(req.body)));
  const chunks = [];
  for await (const c of req) chunks.push(c);
  return Object.fromEntries(new URLSearchParams(Buffer.concat(chunks).toString()));
}

module.exports = async (req, res) => {
  if (req.method !== 'POST') { res.statusCode = 303; res.setHeader('Location', '/login.html'); return res.end(); }
  const form = await readForm(req);
  const user = String(form.username || '').trim().toLowerCase();
  const pass = String(form.password || '').trim();
  res.setHeader('Cache-Control', 'no-store');
  res.statusCode = 303;
  if (same(user, USER.toLowerCase()) && same(pass, PASS)) {
    res.setHeader('Set-Cookie', `bt_session=${token()}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=${MAX_AGE}`);
    res.setHeader('Location', '/');
  } else {
    res.setHeader('Location', '/login.html?e=1');
  }
  res.end();
};
