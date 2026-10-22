"""tools/party_server.py: hosting with a code, joining, profiles, status, events, ping, leaving,
player ids, friends' presence and invites, friend requests."""
from pathlib import Path
import asyncio
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import party_server


class Client:
    def __init__(self, reader, writer):
        self.reader, self.writer = reader, writer

    def send(self, kind, body=b''):
        self.writer.write(struct.pack('<H', 1 + len(body)) + kind.encode() + body)

    async def recv(self):
        n = struct.unpack('<H', await self.reader.readexactly(2))[0]
        body = await self.reader.readexactly(n)
        return chr(body[0]), body[1:]

    async def until(self, kind):
        while True:
            k, body = await asyncio.wait_for(self.recv(), 2)
            if k == kind:
                return body


class PartyServerTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.relay = party_server.Relay(0)
        self.server = await asyncio.start_server(self.relay.serve, '127.0.0.1', 0)
        self.port = self.server.sockets[0].getsockname()[1]
        self.clients = []

    async def asyncTearDown(self):
        for c in self.clients:
            c.writer.close()
        self.server.close()
        await self.server.wait_closed()

    async def connect(self):
        self.clients.append(Client(*await asyncio.open_connection('127.0.0.1', self.port)))
        return self.clients[-1]

    async def test_host_join_by_code_and_events(self):
        host = await self.connect()
        host.send('H', bytes([2]))
        self.assertEqual(await host.until('I'), bytes([0]))
        code = (await host.until('K')).decode()
        self.assertRegex(code, r'^SSR-[A-HJ-NP-Z2-9]{4}$')
        host.send('N', bytes([0]) + struct.pack('<H', 155) + b'Nathan')
        self.assertEqual(await host.until('N'), bytes([0, 1, 0]) + struct.pack('<H', 155) + b'Nathan')

        # Joining by the code typed in lower case without the dash.
        guest = await self.connect()
        guest.send('J', code.replace('-', '').lower().encode())
        self.assertEqual(await guest.until('I'), bytes([1]))
        self.assertEqual((await guest.until('K')).decode(), code)
        self.assertEqual((await guest.until('N'))[:2], bytes([0, 1]))  # the host's hello, host flag
        self.assertEqual(await host.until('j'), bytes([1]))

        guest.send('S', bytes([1]) + struct.pack('<H', 33) + bytes([2]))
        self.assertEqual(await host.until('S'), bytes([1, 1]) + struct.pack('<H', 33) + bytes([2]))
        guest.send('E', bytes([1]) + struct.pack('<H', 163))
        self.assertEqual(await host.until('E'), bytes([1, 1]) + struct.pack('<H', 163))
        walk = bytes([1]) + struct.pack('<HHiiiBH', 0, 33, 0x10000, 0, 0x20000, 1, 97)
        guest.send('O', walk)
        self.assertEqual(await host.until('O'), bytes([1]) + walk)
        guest.send('W', bytes([5]))
        self.assertEqual(await host.until('W'), bytes([1, 5]))
        guest.send('V', bytes([0, 1]))
        self.assertEqual(await host.until('V'), bytes([1, 1]))
        guest.send('Q', struct.pack('<I', 1234))
        self.assertEqual(await guest.until('q'), struct.pack('<I', 1234))

        # Full at two players.
        third = await self.connect()
        third.send('J', code.encode())
        self.assertEqual(await third.until('F'), b'')

        guest.writer.close()
        self.assertEqual(await host.until('l'), bytes([1]))

    async def test_unknown_code_is_refused_and_names_make_parties(self):
        a = await self.connect()
        a.send('J', b'SSR-ZZZZ')
        self.assertEqual(await a.until('F'), b'')
        b = await self.connect()
        b.send('J', b'union')
        self.assertEqual(await b.until('I'), bytes([0]))
        self.assertEqual((await b.until('K')).decode(), 'union')

    async def test_presence_needs_both_friends_and_invites_reach_other_parties(self):
        a_id, b_id, c_id = b'A' * 16, b'B' * 16, b'C' * 16
        a = await self.connect()
        a.send('H', bytes([4]))
        code_a = (await a.until('K')).decode()
        a.send('N', bytes([0, 0, 0]) + b'Alice')
        a.send('U', a_id)
        b = await self.connect()
        b.send('H', bytes([2]))
        code_b = (await b.until('K')).decode()
        b.send('U', b_id)

        # Only Alice lists Bob: she does not see him yet.
        a.send('Y', b_id)
        self.assertEqual(await a.until('y'), b'')
        b.send('Y', a_id)
        self.assertEqual(await b.until('y'), a_id + bytes([1, 4, len(code_a)]) + code_a.encode())
        a.send('Y', b_id)
        self.assertEqual(await a.until('y'), b_id + bytes([1, 2, len(code_b)]) + code_b.encode())

        # Alice invites Bob to her party; a stranger's invite goes nowhere.
        c = await self.connect()
        c.send('J', b'elsewhere')
        c.send('U', c_id)
        c.send('Y', b_id)
        await c.until('y')
        c.send('G', b_id)
        a.send('G', b_id)
        self.assertEqual(await b.until('g'), a_id + bytes([len(code_a)]) + code_a.encode() + b'Alice')

        # Bob joins Alice: his id reaches her party; a friend request and its answer.
        b.writer.close()
        b = await self.connect()
        b.send('J', code_a.encode())
        self.assertEqual(await b.until('U'), bytes([0]) + a_id)
        b.send('U', b_id)
        self.assertEqual(await a.until('U'), bytes([1]) + b_id)
        b.send('Z', bytes([0, 1]))
        self.assertEqual(await a.until('Z'), bytes([1, 1]))
        a.send('Z', bytes([1, 2]))
        self.assertEqual(await b.until('Z'), bytes([0, 2]))
        b.send('T', bytes([1, 3]))
        self.assertEqual(await a.until('T'), bytes([1, 1, 3]))

        # Gone: offline to Alice.
        b.send('Y', a_id)
        await b.until('y')
        a.send('Y', b_id)
        self.assertEqual(len(await a.until('y')), 16 + 3 + len(code_a))
        b.writer.close()
        await a.until('l')
        a.send('Y', b_id)
        self.assertEqual(await a.until('y'), b'')
        self.assertNotIn(b_id.decode(), self.relay.online)

    async def test_version_chat_lock_and_remove(self):
        host = await self.connect()
        host.send('H', bytes([4]))
        await host.until('I')
        code = (await host.until('K')).decode()
        host.send('v', b'1.2.3')
        guest = await self.connect()
        guest.send('J', code.encode())
        await guest.until('I')
        # A newcomer is told the versions already here, and the others hear theirs.
        self.assertEqual(await guest.until('v'), bytes([0]) + b'1.2.3')
        guest.send('v', b'1.0.0')
        self.assertEqual(await host.until('v'), bytes([1]) + b'1.0.0')
        # Chat and quick lines go to the others only, with the sender's number.
        guest.send('c', bytes([0]) + b'hello')
        self.assertEqual(await host.until('c'), bytes([1, 0]) + b'hello')
        # Only the host locks; a locked party turns newcomers away with a reason.
        guest.send('a', bytes([1]))
        host.send('a', bytes([1]))
        self.assertEqual(await guest.until('a'), bytes([1]))
        late = await self.connect()
        late.send('J', code.encode())
        self.assertIn(b'locked', await late.until('F'))
        host.send('a', bytes([0]))
        await host.until('a')
        # Only the host removes someone.
        host.send('k', bytes([0]))
        guest.send('k', bytes([0]))
        host.send('k', bytes([1]))
        await guest.until('x')
        self.assertEqual(await host.until('l'), bytes([1]))

    async def test_lag_keeps_order(self):
        # Messages held back by --lag leave in the order they came, even when many are due at
        # the same moment (timers due together run in any order).
        self.relay.lag = 0.02
        sent = []

        class To:
            class writer:
                is_closing = staticmethod(lambda: False)
                write = staticmethod(sent.append)

        loop = asyncio.get_running_loop()
        now = loop.time()
        loop.time = lambda: now  # all sent within one tick of the clock
        for i in range(500):
            self.relay.send(To, 'D', struct.pack('<H', i))
        del loop.time
        await asyncio.sleep(0.1)
        self.assertEqual([struct.unpack_from('<H', m, 3)[0] for m in sent], list(range(500)))


if __name__ == '__main__':
    unittest.main()
