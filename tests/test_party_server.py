"""tools/party_server.py: hosting with a code, joining, profiles, status, events, ping, leaving."""
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

    async def asyncTearDown(self):
        self.server.close()
        await self.server.wait_closed()

    async def connect(self):
        return Client(*await asyncio.open_connection('127.0.0.1', self.port))

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


if __name__ == '__main__':
    unittest.main()
