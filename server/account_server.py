#!/usr/bin/env python3
"""Account server for Johto Online: Discord sign-in, signed session tokens, SQLite.

Standard library only. Optional: the relay (server/relay_service.py) can ask for a token
before letting a player in; without this service the relay works as it always did.

Sign-in is a device flow, so it works from a controller: the game calls POST /device/start,
shows a short code, opens /login?code=CODE in the browser (which shows the same code and a
Discord button), and polls POST /device/poll until the browser side has finished.

    python3 server/account_server.py serve          run (settings from environment variables)
    python3 server/account_server.py ban DISCORD_ID   (also unban, list)

Environment (see SERVER.md):
  ACCOUNT_SECRET          required: signs tokens; the relay needs the same value
  PUBLIC_URL              required: where players reach this service, e.g. https://accounts.example.org
  DISCORD_CLIENT_ID, DISCORD_CLIENT_SECRET   the Discord application
  DISCORD_REDIRECT_URI    default PUBLIC_URL/callback (must be listed in the Discord application)
  DISCORD_BOT_TOKEN, DISCORD_GUILD_ID, DISCORD_BETA_ROLE_ID   all three: only members with the role may sign in
  ACCOUNT_DB              SQLite file, default accounts.db
  ACCOUNT_HOST, ACCOUNT_PORT   default 127.0.0.1 and 7380 (put a reverse proxy with TLS in front)
  TRUST_PROXY=1           take the client address from X-Forwarded-For (only behind your own proxy)
  TOKEN_DAYS              how long a token lasts, default 30

Tables other features will use later (mail and gifts, the Global Box, guilds) exist as empty
stubs; nothing reads or writes them yet.
"""
import collections
import html
import http.server
import json
import os
import secrets
import sqlite3
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tokens

CODE_LETTERS = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'
DEVICE_LIFE = 600       # seconds a sign-in may take
DEVICE_MAX = 2000       # pending sign-ins at once
RATE = (120, 60)        # requests per client address per seconds

SCHEMA = """
CREATE TABLE IF NOT EXISTS accounts (
    id TEXT PRIMARY KEY,          -- Discord user id
    name TEXT NOT NULL,           -- display name at the last sign-in
    created INTEGER NOT NULL,
    last_login INTEGER NOT NULL,
    banned INTEGER NOT NULL DEFAULT 0,
    beta INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS devices (
    device_code TEXT PRIMARY KEY, -- secret, held by the game
    user_code TEXT UNIQUE NOT NULL, -- shown in the game and the browser
    status TEXT NOT NULL,         -- pending, ok, denied
    token TEXT, name TEXT, message TEXT,
    expires INTEGER NOT NULL);
-- Stubs for future features (unused):
CREATE TABLE IF NOT EXISTS mail (id INTEGER PRIMARY KEY, to_id TEXT, from_id TEXT, kind TEXT, body TEXT, created INTEGER, claimed INTEGER DEFAULT 0);
CREATE TABLE IF NOT EXISTS global_box (id INTEGER PRIMARY KEY, owner_id TEXT, data BLOB, created INTEGER);
CREATE TABLE IF NOT EXISTS guilds (id INTEGER PRIMARY KEY, name TEXT UNIQUE, owner_id TEXT, created INTEGER);
CREATE TABLE IF NOT EXISTS guild_members (guild_id INTEGER, account_id TEXT, role TEXT, PRIMARY KEY (guild_id, account_id));
"""


class Discord:
    """The three calls made to Discord. Tests replace this class."""
    API = 'https://discord.com/api/v10'

    def __init__(self, cfg):
        self.cfg = cfg

    def _call(self, url, data=None, headers=None):
        req = urllib.request.Request(url, data=urllib.parse.urlencode(data).encode() if data else None,
                                     headers={'User-Agent': 'JohtoOnline (accounts, 1)', **(headers or {})})
        with urllib.request.urlopen(req, timeout=10) as r:
            return json.load(r)

    def exchange(self, code):
        """The user's access token for an authorization code."""
        c = self.cfg
        return self._call(self.API + '/oauth2/token', {
            'client_id': c['client_id'], 'client_secret': c['client_secret'], 'grant_type': 'authorization_code',
            'code': code, 'redirect_uri': c['redirect']})['access_token']

    def user(self, access):
        """{'id', 'username', 'global_name'} of the user."""
        return self._call(self.API + '/users/@me', headers={'Authorization': 'Bearer ' + access})

    def has_role(self, user_id):
        c = self.cfg
        try:
            m = self._call(f"{self.API}/guilds/{c['guild']}/members/{user_id}", headers={'Authorization': 'Bot ' + c['bot']})
        except urllib.error.HTTPError:
            return False  # not in the guild
        return c['role'] in m.get('roles', [])


def page(title, body, status=200):
    doc = ('<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">'
           f'<title>{html.escape(title)}</title><style>body{{font:18px system-ui;max-width:28em;margin:3em auto;padding:0 1em}}'
           '.code{font:700 2.4em monospace;letter-spacing:.15em;text-align:center;margin:.6em 0}'
           'a.b{display:block;background:#5865f2;color:#fff;text-align:center;padding:.8em;border-radius:.5em;text-decoration:none}</style>'
           f'<h1>{html.escape(title)}</h1>{body}')
    return status, {'Content-Type': 'text/html; charset=utf-8'}, doc.encode()


def js(obj, status=200):
    return status, {'Content-Type': 'application/json'}, json.dumps(obj).encode()


class App:
    def __init__(self, cfg, discord=None, clock=time.time):
        self.cfg = cfg
        self.secret = cfg['secret']
        self.discord = discord or Discord(cfg)
        self.clock = clock
        self.db = sqlite3.connect(cfg.get('db', 'accounts.db'), check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.executescript(SCHEMA)
        self.lock = threading.Lock()
        self.hits = collections.defaultdict(collections.deque)

    # -- storage --
    def q(self, sql, *args):
        with self.lock:
            rows = self.db.execute(sql, args).fetchall()
            self.db.commit()
            return rows

    def account(self, uid):
        rows = self.q('SELECT * FROM accounts WHERE id=?', uid)
        return rows[0] if rows else None

    def set_ban(self, uid, banned):
        return len(self.q('UPDATE accounts SET banned=? WHERE id=? RETURNING id', int(banned), uid)) == 1

    # -- state parameter: signed, so nothing is stored between /authorize and /callback --
    def sign_state(self, user_code):
        body = f'{user_code or "-"}.{int(self.clock()) + DEVICE_LIFE}.{secrets.token_hex(4)}'
        return body + '.' + tokens._sign(self.secret, 'state.' + body)

    def read_state(self, state):
        try:
            code, exp, nonce, sig = state.split('.')
            if not tokens.hmac.compare_digest(sig, tokens._sign(self.secret, f'state.{code}.{exp}.{nonce}')) or int(exp) < self.clock():
                return None
            return '' if code == '-' else code
        except ValueError:
            return None

    def limited(self, who):
        h, now = self.hits[who], self.clock()
        while h and h[0] < now - RATE[1]:
            h.popleft()
        h.append(now)
        if len(self.hits) > 5000:
            self.hits = collections.defaultdict(collections.deque, {who: h})
        return len(h) > RATE[0]

    # -- routes: (method, path, query dict, bearer token, json body) -> (status, headers, bytes) --
    def handle(self, method, path, query, bearer, body, who='-'):
        if self.limited(who):
            return js({'error': 'slow down'}, 429)
        route = getattr(self, 'r_' + path.strip('/').replace('/', '_'), None) if path != '/' else None
        if route is None or method not in route.methods:
            return js({'error': 'not found'}, 404)
        return route(query, bearer, body)

    def r_login(self, query, bearer, body):
        code = query.get('code', '')
        if not code:
            return 302, {'Location': '/authorize'}, b''
        if not self.device_row(code):
            return page('Sign-in expired', '<p>Go back to the game and start the sign-in again.</p>', 410)
        return page('Sign in to Johto Online',
                    f'<p>Is this the code shown in your game?</p><div class=code>{html.escape(code)}</div>'
                    f'<a class=b href="/authorize?code={urllib.parse.quote(code)}">Yes, continue with Discord</a>'
                    '<p>If it is not the same, close this page: someone else may be trying to sign you in.</p>')
    r_login.methods = ('GET',)

    def r_authorize(self, query, bearer, body):
        c = self.cfg
        url = 'https://discord.com/oauth2/authorize?' + urllib.parse.urlencode({
            'client_id': c['client_id'], 'response_type': 'code', 'scope': 'identify',
            'redirect_uri': c['redirect'], 'state': self.sign_state(query.get('code', '')), 'prompt': 'none'})
        return 302, {'Location': url}, b''
    r_authorize.methods = ('GET',)

    def device_row(self, user_code):
        rows = self.q('SELECT * FROM devices WHERE user_code=? AND expires>?', user_code, int(self.clock()))
        return rows[0] if rows else None

    def r_callback(self, query, bearer, body):
        user_code = self.read_state(query.get('state', ''))
        if user_code is None or 'code' not in query:
            return page('Sign-in failed', '<p>This link is not valid any more. Start again from the game.</p>', 400)
        try:
            who = self.discord.user(self.discord.exchange(query['code']))
        except (urllib.error.URLError, KeyError, ValueError):
            return page('Sign-in failed', '<p>Discord did not accept the sign-in. Try again.</p>', 502)
        uid, name = str(who['id']), (who.get('global_name') or who.get('username') or uid)[:32]
        problem = None
        beta = 0
        if self.cfg.get('role'):
            beta = int(self.discord.has_role(uid))
            if not beta:
                problem = 'This server is for beta testers only. Ask for the beta role on Discord.'
        now = int(self.clock())
        self.q('INSERT INTO accounts (id,name,created,last_login,beta) VALUES (?,?,?,?,?) '
               'ON CONFLICT(id) DO UPDATE SET name=excluded.name,last_login=excluded.last_login,beta=excluded.beta',
               uid, name, now, now, beta)
        if self.account(uid)['banned']:
            problem = 'This account is banned.'
        row = self.device_row(user_code) if user_code else None
        if user_code and row is None:
            return page('Sign-in expired', '<p>Go back to the game and start again.</p>', 410)
        if problem:
            if row:
                self.q("UPDATE devices SET status='denied', message=? WHERE device_code=?", problem, row['device_code'])
            return page('Cannot sign in', f'<p>{html.escape(problem)}</p>', 403)
        tok = tokens.make(self.secret, uid, name, self.cfg.get('days', 30), self.clock())
        if row:
            self.q("UPDATE devices SET status='ok', token=?, name=? WHERE device_code=?", tok, name, row['device_code'])
        return page('Signed in', f'<p>Hello, {html.escape(name)}. You can go back to the game.</p>'
                    + (f'<div class=code>{html.escape(user_code)}</div>' if user_code else ''))
    r_callback.methods = ('GET',)

    def r_device_start(self, query, bearer, body):
        now = int(self.clock())
        self.q('DELETE FROM devices WHERE expires<?', now)
        if self.q('SELECT count(*) AS n FROM devices')[0]['n'] >= DEVICE_MAX:
            return js({'error': 'busy'}, 503)
        dev = secrets.token_urlsafe(24)
        user = ''.join(secrets.choice(CODE_LETTERS) for _ in range(8))
        user = user[:4] + '-' + user[4:]
        self.q("INSERT INTO devices (device_code,user_code,status,expires) VALUES (?,?,'pending',?)", dev, user, now + DEVICE_LIFE)
        return js({'device_code': dev, 'user_code': user, 'login_url': f"{self.cfg['public']}/login?code={user}",
                   'interval': 3, 'expires_in': DEVICE_LIFE})
    r_device_start.methods = ('POST',)

    def r_device_poll(self, query, bearer, body):
        rows = self.q('SELECT * FROM devices WHERE device_code=?', str((body or {}).get('device_code', '')))
        if not rows or rows[0]['expires'] < self.clock():
            return js({'status': 'expired'})
        r = rows[0]
        if r['status'] == 'pending':
            return js({'status': 'pending'})
        self.q('DELETE FROM devices WHERE device_code=?', r['device_code'])  # a token is handed over once
        if r['status'] == 'ok':
            return js({'status': 'ok', 'token': r['token'], 'name': r['name']})
        return js({'status': 'denied', 'message': r['message']})
    r_device_poll.methods = ('POST',)

    def check(self, token):
        """(payload, account row) for a good token of an account that exists, else (None, None)."""
        data = tokens.verify(self.secret, token or '', self.clock())
        acc = self.account(data['sub']) if data else None
        return (data, acc) if acc else (None, None)

    def r_me(self, query, bearer, body):
        data, acc = self.check(bearer)
        if not data:
            return js({'error': 'not signed in'}, 401)
        if acc['banned']:
            return js({'error': 'banned'}, 403)
        return js({'id': acc['id'], 'name': acc['name'], 'created': acc['created'], 'beta': bool(acc['beta'])})
    r_me.methods = ('GET',)

    def r_token_verify(self, query, bearer, body):
        data, acc = self.check((body or {}).get('token') or bearer)
        if not data:
            return js({'valid': False})
        return js({'valid': not acc['banned'], 'banned': bool(acc['banned']), 'id': acc['id'], 'name': acc['name']})
    r_token_verify.methods = ('POST', 'GET')


def make_handler(app):
    class H(http.server.BaseHTTPRequestHandler):
        server_version = 'JohtoAccounts'

        def do(self):
            u = urllib.parse.urlsplit(self.path)
            query = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
            try:
                n = min(int(self.headers.get('Content-Length') or 0), 4096)
                body = json.loads(self.rfile.read(n) or 'null') if n else None
                if body is not None and not isinstance(body, dict):
                    raise ValueError
            except ValueError:
                body = None
            auth = self.headers.get('Authorization', '')
            who = self.client_address[0]
            if os.environ.get('TRUST_PROXY') == '1':
                who = self.headers.get('X-Forwarded-For', who).split(',')[-1].strip()
            status, headers, data = app.handle(self.command, u.path, query, auth[7:] if auth.startswith('Bearer ') else None, body, who)
            self.send_response(status)
            for k, v in {**headers, 'Content-Length': str(len(data)), 'Cache-Control': 'no-store'}.items():
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(data)

        do_GET = do_POST = do

        def log_message(self, fmt, *args):  # never log the query: it holds codes
            print(f'{self.address_string()} {self.command} {self.path.split("?")[0]} {args[1] if len(args) > 1 else ""}', flush=True)
    return H


def config_from_env(env=os.environ):
    public = env.get('PUBLIC_URL', '').rstrip('/')
    if not env.get('ACCOUNT_SECRET') or len(env['ACCOUNT_SECRET']) < 16 or not public:
        sys.exit('Set ACCOUNT_SECRET (16 characters or more) and PUBLIC_URL; see SERVER.md.')
    cfg = {'secret': env['ACCOUNT_SECRET'], 'public': public, 'db': env.get('ACCOUNT_DB', 'accounts.db'),
           'client_id': env.get('DISCORD_CLIENT_ID', ''), 'client_secret': env.get('DISCORD_CLIENT_SECRET', ''),
           'redirect': env.get('DISCORD_REDIRECT_URI') or public + '/callback', 'days': int(env.get('TOKEN_DAYS', 30))}
    if env.get('DISCORD_BETA_ROLE_ID'):
        cfg.update(role=env['DISCORD_BETA_ROLE_ID'], guild=env.get('DISCORD_GUILD_ID', ''), bot=env.get('DISCORD_BOT_TOKEN', ''))
        if not cfg['guild'] or not cfg['bot']:
            sys.exit('DISCORD_BETA_ROLE_ID needs DISCORD_GUILD_ID and DISCORD_BOT_TOKEN too.')
    return cfg


def main(argv):
    cmd = argv[1] if len(argv) > 1 else 'serve'
    app = App(config_from_env())
    if cmd in ('ban', 'unban') and len(argv) == 3:
        print('done' if app.set_ban(argv[2], cmd == 'ban') else 'no such account')
    elif cmd == 'list':
        for r in app.q('SELECT * FROM accounts ORDER BY created'):
            print(r['id'], r['name'], 'BANNED' if r['banned'] else '')
    elif cmd == 'serve':
        host, port = os.environ.get('ACCOUNT_HOST', '127.0.0.1'), int(os.environ.get('ACCOUNT_PORT', 7380))
        srv = http.server.ThreadingHTTPServer((host, port), make_handler(app))
        print(f'account server on {host}:{port}, public address {app.cfg["public"]}', flush=True)
        try:
            srv.serve_forever()
        except KeyboardInterrupt:
            pass
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv)
