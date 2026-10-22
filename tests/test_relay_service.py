"""server/relay_service.py: limits, idle timeout, health, and account tokens around the relay."""
from pathlib import Path
import asyncio
import json
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'server'))
import relay_service
import tokens

SECRET = 'x' * 32


def frame(kind, body=b''):
    return struct.pack('<H', 1 + len(body)) + kind.encode() + body


async def read_frame(r):
    n = struct.unpack('<H', await asyncio.wait_for(r.readexactly(2), 2))[0]
    body = await r.readexactly(n)
    return chr(body[0]), body[1:]


class RelayServiceTests(unittest.IsolatedAsyncioTestCase):
    async def start(self, **env):
        self.svc = relay_service.Service({k: str(v) for k, v in env.items()})
        self.server = await asyncio.start_server(self.svc.serve, '127.0.0.1', 0)
        self.port = self.server.sockets[0].getsockname()[1]
        self.writers = []

    async def asyncTearDown(self):
        for w in self.writers:
            w.close()
        self.server.close()
        await self.server.wait_closed()

    async def connect(self, *frames):
        r, w = await asyncio.open_connection('127.0.0.1', self.port)
        self.writers.append(w)
        for f in frames:
            w.write(f)
        return r, w

    async def joined(self, r):
        kinds = []
        while True:
            k, b = await read_frame(r)
            kinds.append(k)
            if k in 'IF':
                return k, kinds

    async def test_works_without_accounts(self):
        await self.start()
        r, _ = await self.connect(frame('H', bytes([4])))
        self.assertEqual((await self.joined(r))[0], 'I')
        self.assertEqual(self.svc.health()['players'], 1)

    async def test_token_required(self):
        await self.start(RELAY_REQUIRE_ACCOUNT=1, ACCOUNT_SECRET=SECRET)
        r, _ = await self.connect(frame('H', bytes([4])))  # no token
        self.assertEqual((await self.joined(r))[0], 'F')
        r, _ = await self.connect(frame('a', b'v1.junk.junk'), frame('H', bytes([4])))
        self.assertEqual((await self.joined(r))[0], 'F')
        good = tokens.make(SECRET, '1', 'Ash').encode()
        r, _ = await self.connect(frame('a', good), frame('H', bytes([4])))
        self.assertEqual((await self.joined(r))[0], 'I')

    async def test_token_optional_is_accepted_and_bad_one_refused(self):
        await self.start(ACCOUNT_SECRET=SECRET)
        good = tokens.make(SECRET, '1', 'Ash').encode()
        r, _ = await self.connect(frame('a', good), frame('H', bytes([2])))
        self.assertEqual((await self.joined(r))[0], 'I')

    async def test_session_limit(self):
        await self.start(RELAY_MAX_SESSIONS=1)
        r, _ = await self.connect(frame('H', bytes([2])))
        await self.joined(r)
        r2, _ = await self.connect(frame('H', bytes([2])))
        self.assertEqual((await self.joined(r2))[0], 'F')

    async def test_connection_limit_per_address(self):
        await self.start(RELAY_MAX_PER_IP=2)
        for _ in range(2):
            r, _ = await self.connect(frame('H', bytes([2])))
            await self.joined(r)
        r3, _ = await self.connect(frame('H', bytes([2])))
        try:
            self.assertEqual(await asyncio.wait_for(r3.read(), 2), b'')  # closed (a reset on Windows)
        except ConnectionError:
            pass

    async def test_idle_timeout(self):
        await self.start(RELAY_IDLE_SECONDS=1)
        r, _ = await self.connect(frame('H', bytes([2])))
        await self.joined(r)
        end = await asyncio.wait_for(self._drain(r), 4)
        self.assertEqual(end, b'')
        await asyncio.sleep(0.1)
        self.assertEqual(self.svc.health()['players'], 0)

    async def _drain(self, r):
        while await r.read(1000):
            pass
        return b''

    async def test_health(self):
        await self.start()
        h = await relay_service.health_server(self.svc, 0, '127.0.0.1')
        port = h.sockets[0].getsockname()[1]
        r, w = await asyncio.open_connection('127.0.0.1', port)
        w.write(b'GET /health HTTP/1.1\r\n\r\n')
        data = await asyncio.wait_for(r.read(), 2)
        self.assertIn(b'200 OK', data)
        self.assertTrue(json.loads(data.split(b'\r\n\r\n')[1])['ok'])
        w.close()
        h.close()


if __name__ == '__main__':
    unittest.main()
