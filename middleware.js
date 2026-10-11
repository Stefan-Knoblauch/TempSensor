// Vercel Routing Middleware: password gate for the whole deployment.
// Runs on Vercel's edge before any file is served, so pages are never sent to a
// browser that has not signed in. Credentials can be overridden with the project
// environment variables VIEWER_USER, VIEWER_PASS and VIEWER_SECRET.

export const config = { matcher: '/:path*' };

const USER = process.env.VIEWER_USER || 'Bigetrontemp';
const PASS = process.env.VIEWER_PASS || 'temp';
const SECRET = process.env.VIEWER_SECRET || 'bigetron-rail-sensor';
const COOKIE = 'bt_session';
const MAX_AGE = 60 * 60 * 12; // 12 h
const PUBLIC = new Set(['/login.html', '/favicon.ico']);

// Same effect as next() from @vercel/functions: continue to the requested file.
const next = () => new Response(null, { headers: { 'x-middleware-next': '1' } });

const redirect = (url, cookie) => {
  const headers = new Headers({ Location: url, 'Cache-Control': 'no-store' });
  if (cookie) headers.append('Set-Cookie', cookie);
  return new Response(null, { status: 303, headers });
};

async function sessionToken() {
  const data = new TextEncoder().encode(`${USER}\n${PASS}\n${SECRET}`);
  const hash = await crypto.subtle.digest('SHA-256', data);
  return [...new Uint8Array(hash)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

function sameText(a, b) {
  if (a.length !== b.length) return false;
  let diff = 0;
  for (let i = 0; i < a.length; i++) diff |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return diff === 0;
}

function readCookie(request, name) {
  const raw = request.headers.get('cookie') || '';
  const hit = raw.split(';').map((c) => c.trim()).find((c) => c.startsWith(name + '='));
  return hit ? decodeURIComponent(hit.slice(name.length + 1)) : '';
}

export default async function middleware(request) {
  const url = new URL(request.url);
  const path = url.pathname;

  if ((path === '/login' || path === '/login.html') && request.method === 'POST') {
    // Parse the form body directly so it works whatever content type the browser sends.
    const body = new URLSearchParams(await request.text());
    const user = (body.get('username') || '').trim().toLowerCase();
    const pass = (body.get('password') || '').trim();
    const ok = sameText(user, USER.toLowerCase()) && sameText(pass, PASS);
    if (!ok) return redirect('/login.html?e=1');
    const cookie = `${COOKIE}=${await sessionToken()}; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=${MAX_AGE}`;
    return redirect('/', cookie);
  }
  if (path === '/logout') {
    return redirect('/login.html', `${COOKIE}=; Path=/; HttpOnly; Secure; SameSite=Lax; Max-Age=0`);
  }
  if (PUBLIC.has(path)) return next();

  if (sameText(readCookie(request, COOKIE), await sessionToken())) return next();
  return redirect('/login.html');
}
