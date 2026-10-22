#!/usr/bin/env python3
"""The party relay (tools/party_server.py) as a service: limits, health check, optional accounts.

It runs the relay unchanged and adds, around it: a connection limit (total and per address),
an idle timeout, a limit on sessions, a health endpoint, a clean stop on SIGTERM, and
optionally a required account token. Settings come from environment variables:

  RELAY_PORT          TCP port for the game, default 7373
  RELAY_HEALTH_PORT   HTTP port for GET /health, default 7374 (0: off)
  RELAY_MAX_CONNECTIONS   default 200;  RELAY_MAX_PER_IP   default 8
  RELAY_MAX_SESSIONS  parties at once, default 50
  RELAY_IDLE_SECONDS  close a connection that sends nothing for this long, default 120
                      (the game pings every few seconds, so a live one never idles)
  RELAY_REQUIRE_ACCOUNT=1  only players with a valid token (from server/account_server.py) get in
  ACCOUNT_SECRET      the account server's secret, to check tokens
  ACCOUNT_URL         optional: ask the account server too, so a ban takes effect at once
  RELAY_LAG_MS        added delay for testing, default 0

A signed-in game sends the token first, as the message 'a' + token; one that does not goes
straight to J or H as before. The relay accepts both unless RELAY_REQUIRE_ACCOUNT is set.
"""
import asyncio
import json
import os
import signal
import struct
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'tools'))
import party_server
import tokens

FULL = b'\x01\x00F'  # the relay's own "cannot join" answer


class Prefixed:
    """Reads from `first` (bytes already taken off the stream), then from the stream, with an idle timeout."""

    def __init__(self, reader, first, idle):
        self.reader, self.buf, self.idle = reader, first, idle

    async def readexactly(self, n):
        out, self.buf = self.buf[:n], self.buf[n:]
        if len(out) < n:
            try:
                out += await asyncio.wait_for(self.reader.readexactly(n - len(out)), self.idle)
            except asyncio.TimeoutError:
                raise ConnectionError('idle')
        return out


class Service(party_server.Relay):
    def __init__(self, env=os.environ, lag=0):
        super().__init__(lag)
        g = lambda k, d: int(env.get(k, d))
        self.max_conn, self.max_ip, self.max_sessions = g('RELAY_MAX_CONNECTIONS', 200), g('RELAY_MAX_PER_IP', 8), g('RELAY_MAX_SESSIONS', 50)
        self.idle = g('RELAY_IDLE_SECONDS', 120)
        self.require = env.get('RELAY_REQUIRE_ACCOUNT') == '1'
        self.secret, self.account_url = env.get('ACCOUNT_SECRET', ''), env.get('ACCOUNT_URL', '').rstrip('/')
        if self.require and not self.secret:
            sys.exit('RELAY_REQUIRE_ACCOUNT needs ACCOUNT_SECRET.')
        self.conns = {}      # address -> connections
        self.total = 0
        self.writers = set()
        self.banned = {}     # token -> (until, banned) for the account server's answers
        self.started = time.time()

    def enter(self, kind, body, writer, *a, **k):
        name = body[:32].decode('utf-8', 'replace').strip()
        # Only new sessions are stopped: a code that does not exist is refused by the relay anyway.
        if len(self.parties) >= self.max_sessions and (kind == 'H' or (name not in self.parties and not name.upper().startswith('SSR'))):
            writer.write(FULL)
            return None
        return super().enter(kind, body, writer, *a, **k)

    def player_count(self):
        return sum(1 for p in self.parties.values() for q in p.slots if q)

    def accepted(self, token):
        """Whether this token may play now."""
        data = tokens.verify(self.secret, token) if self.secret else None
        if not data:
            return False
        if not self.account_url:
            return True
        hit = self.banned.get(token)
        if hit and hit[0] > time.time():
            return hit[1]
        try:
            req = urllib.request.Request(self.account_url + '/token/verify', json.dumps({'token': token}).encode())
            ok = json.load(urllib.request.urlopen(req, timeout=3)).get('valid', False)
        except (OSError, ValueError):
            return True  # the account server is down: the signature is good, so let the player in
        self.banned[token] = (time.time() + 60, ok)
        if len(self.banned) > 5000:
            self.banned.clear()
        return ok

    async def serve(self, reader, writer):
        who = (writer.get_extra_info('peername') or ('?',))[0]
        if self.total >= self.max_conn or self.conns.get(who, 0) >= self.max_ip:
            writer.close()
            return
        self.total += 1
        self.conns[who] = self.conns.get(who, 0) + 1
        self.writers.add(writer)
        try:
            # The first message, with a short wait: a connection that never speaks is dropped.
            head = await asyncio.wait_for(reader.readexactly(2), 15)
            n = struct.unpack('<H', head)[0]
            body = await asyncio.wait_for(reader.readexactly(n), 15)
            if body[:1] == b'a':
                if not self.accepted(body[1:].decode('ascii', 'replace')):
                    writer.write(FULL)
                    return
                first = b''
            else:
                if self.require:
                    writer.write(FULL)
                    return
                first = head + body
            await super().serve(Prefixed(reader, first, self.idle), writer)
        except (asyncio.TimeoutError, asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            self.total -= 1
            self.conns[who] -= 1
            if not self.conns[who]:
                del self.conns[who]
            self.writers.discard(writer)
            writer.close()

    def health(self):
        return {'ok': True, 'sessions': len(self.parties), 'players': self.player_count(), 'connections': self.total,
                'uptime': int(time.time() - self.started), 'accounts': 'required' if self.require else 'optional'}


async def health_server(svc, port, host=None):
    async def handle(reader, writer):
        try:
            line = await asyncio.wait_for(reader.readline(), 5)
            ok = line.startswith(b'GET /health')
            body = json.dumps(svc.health() if ok else {'error': 'not found'}).encode()
            writer.write(b'HTTP/1.1 %s\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n' %
                         (b'200 OK' if ok else b'404 Not Found', len(body)) + body)
            await writer.drain()
        except (asyncio.TimeoutError, ConnectionError):
            pass
        finally:
            writer.close()
    return await asyncio.start_server(handle, host, port)


async def main():
    env = os.environ
    svc = Service(env, float(env.get('RELAY_LAG_MS', 0)))
    port = int(env.get('RELAY_PORT', 7373))
    game = await asyncio.start_server(svc.serve, None, port)
    hp = int(env.get('RELAY_HEALTH_PORT', 7374))
    health = await health_server(svc, hp) if hp else None
    print(f'relay on TCP {port}, health on {hp or "off"}, accounts {svc.health()["accounts"]}', flush=True)
    stop = asyncio.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        try:
            asyncio.get_running_loop().add_signal_handler(sig, stop.set)
        except NotImplementedError:  # Windows
            signal.signal(sig, lambda *_: asyncio.get_running_loop().call_soon_threadsafe(stop.set))
    await stop.wait()
    print('stopping', flush=True)
    game.close()
    if health:
        health.close()
    for w in list(svc.writers):
        w.close()
    await asyncio.sleep(0.2)


if __name__ == '__main__':
    asyncio.run(main())
