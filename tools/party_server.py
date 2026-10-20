#!/usr/bin/env python3
"""Relay server for ssrecomp parties (frontend/party.cpp).

Up to four games join a party by name. The games' wireless layer is taken over by the
frontend, so what passes through here are its messages: parents announcing themselves
(beacons, as the Union Room and the Pokemon Center clubs do), children asking to join a
parent, and the data the joined games exchange. Nothing is stored.

    python3 tools/party_server.py [--port 7373] [--lag MS]

--lag holds every message back by MS milliseconds before passing it on, to try how the
game copes with players far apart (a round trip between two players is then 2*MS).

Messages, both ways: u16 length (little-endian), then a kind letter and its contents.
  to the server:   J name | B beacon | b | C parent commtype | A child aid | R child | D data | L
  from the server: I player | F | B player beacon | b player | C child commtype | A aid | R
                   | D player data | X child | P
"""
import argparse
import asyncio
import struct

MAX_PLAYERS = 4


class Player:
    def __init__(self, party, slot, writer):
        self.party, self.slot, self.writer = party, slot, writer
        self.beacon = None
        self.parent = None    # as a child: the parent Player, once accepted
        self.asked = None     # as a child: the parent asked
        self.children = set()


class Relay:
    def __init__(self, lag):
        self.lag = lag / 1000.0
        self.parties = {}

    def send(self, to, kind, body=b''):
        msg = struct.pack('<H', 1 + len(body)) + kind.encode() + body
        if self.lag:
            asyncio.get_running_loop().call_later(self.lag, self.write, to, msg)
        else:
            self.write(to, msg)

    @staticmethod
    def write(to, msg):
        if not to.writer.is_closing():
            to.writer.write(msg)

    def others(self, p):
        return [q for q in self.parties[p.party] if q and q is not p]

    def leave_session(self, p):
        if p.parent:
            p.parent.children.discard(p)
            self.send(p.parent, 'X', bytes([p.slot]))
        elif p.asked:
            self.send(p.asked, 'X', bytes([p.slot]))
        for c in list(p.children):
            c.parent = None
            self.send(c, 'P')
        p.children.clear()
        p.parent = p.asked = None

    def message(self, p, kind, body):
        if kind == 'B':
            p.beacon = body
            for q in self.others(p):
                self.send(q, 'B', bytes([p.slot]) + body)
        elif kind == 'b':
            p.beacon = None
            for q in self.others(p):
                self.send(q, 'b', bytes([p.slot]))
        elif kind == 'C' and len(body) >= 2:
            parent = self.parties[p.party][body[0]] if body[0] < MAX_PLAYERS else None
            self.leave_session(p)
            if parent is None or parent is p:
                self.send(p, 'R')
                return
            p.asked = parent
            self.send(parent, 'C', bytes([p.slot, body[1]]))
        elif kind in 'AR' and len(body) >= 1 and body[0] < MAX_PLAYERS:
            child = self.parties[p.party][body[0]]
            if child is None or child.asked is not p:
                return
            child.asked = None
            if kind == 'A' and len(body) >= 2:
                child.parent = p
                p.children.add(child)
                self.send(child, 'A', body[1:2])
            else:
                self.send(child, 'R')
        elif kind == 'D':
            # A parent's data goes to all its children, a child's to its parent.
            for q in (p.children if p.children else [p.parent] if p.parent else []):
                self.send(q, 'D', bytes([p.slot]) + body)
        elif kind == 'L':
            self.leave_session(p)

    async def serve(self, reader, writer):
        p = None
        try:
            while True:
                head = await reader.readexactly(2)
                body = await reader.readexactly(struct.unpack('<H', head)[0])
                if not body:
                    continue
                kind, body = chr(body[0]), body[1:]
                if p is None:
                    if kind != 'J':
                        return
                    name = body[:32].decode('utf-8', 'replace')
                    slots = self.parties.setdefault(name, [None] * MAX_PLAYERS)
                    if None not in slots:
                        writer.write(b'\x01\x00F')
                        return
                    slot = slots.index(None)
                    p = slots[slot] = Player(name, slot, writer)
                    writer.write(b'\x02\x00I' + bytes([slot]))
                    for q in self.others(p):
                        if q.beacon is not None:
                            self.send(p, 'B', bytes([q.slot]) + q.beacon)
                    print(f'player {slot + 1} joined party {name!r}', flush=True)
                    continue
                self.message(p, kind, body)
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            if p:
                self.leave_session(p)
                for q in self.others(p):
                    self.send(q, 'b', bytes([p.slot]))
                self.parties[p.party][p.slot] = None
                if not any(self.parties[p.party]):
                    del self.parties[p.party]
                print(f'player {p.slot + 1} left party {p.party!r}', flush=True)
            writer.close()


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', type=int, default=7373)
    ap.add_argument('--lag', type=float, default=0, help='milliseconds to hold every message back')
    args = ap.parse_args()
    relay = Relay(args.lag)
    server = await asyncio.start_server(relay.serve, '0.0.0.0', args.port)
    print(f'party server on TCP port {args.port}, lag {args.lag:g} ms', flush=True)
    async with server:
        await server.serve_forever()


if __name__ == '__main__':
    asyncio.run(main())
