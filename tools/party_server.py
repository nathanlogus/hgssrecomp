#!/usr/bin/env python3
"""Relay server for ssrecomp parties (frontend/party.cpp).

Up to four games join a party. The games' wireless layer is taken over by the frontend,
so what passes through here are its messages: parents announcing themselves (beacons,
as the Union Room and the Pokemon Center clubs do), children asking to join a parent,
and the data the joined games exchange. Around that, what the HUD shows: each player's
name and colour, where they are, the round trip to the server, and events (a catch, a
badge). Nothing is stored; a party is gone when its last player leaves.

A player hosts a party and gets a code (SSR-XXXX) for the others to join with, or joins
a party by name (`--party-code union`), which is made when the first player arrives.

    python3 tools/party_server.py [--port 7373] [--lag MS]

--lag holds every message back by MS milliseconds before passing it on, to try how the
game copes with players far apart (a round trip between two players is then 2*MS).

Messages, both ways: u16 length (little-endian), then a kind letter and its contents.
  to the server:
    J name             join the party with that name or code
    H max              host a new party for up to `max` players (2-4)
    N colour u16 partner name   who this player is (sent after joining, and on changes)
    S state u16 map badges [u16 rtt]   where this player is (and their round trip, ms)
    E kind u16 arg     an event (1 caught a Pokemon: species; 2 earned a badge: count)
    Q t                ping: answered with q t
    M max              the host changes the number of players
    B beacon | b | C parent commtype | A child aid | R child | D data | L   (the game's link)
  from the server:
    I player | F (full) | K code (the party's code, after H or J)
    N player flags colour u16 partner name   (flags: 1 host)
    j player | l player    (joined, left)
    S player state u16 map badges [u16 rtt] | E player kind u16 arg | q t | M max
    B player beacon | b player | C child commtype | A aid | R | D player data | X child | P
"""
import argparse
import asyncio
import secrets
import struct

MAX_PLAYERS = 4
CODE_LETTERS = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'  # no O/0 or I/1


class Player:
    def __init__(self, party, slot, writer):
        self.party, self.slot, self.writer = party, slot, writer
        self.beacon = None
        self.parent = None    # as a child: the parent Player, once accepted
        self.asked = None     # as a child: the parent asked
        self.children = set()
        self.hello = None     # colour, partner, name (the N message without player and flags)
        self.status = None    # the S message's contents


class Party:
    def __init__(self, name, max_players=MAX_PLAYERS):
        self.name = name
        self.slots = [None] * MAX_PLAYERS
        self.max = max_players
        self.host = None      # slot of the host, if hosted


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
        return [q for q in p.party.slots if q and q is not p]

    def new_code(self):
        while True:
            code = 'SSR-' + ''.join(secrets.choice(CODE_LETTERS) for _ in range(4))
            if code not in self.parties:
                return code

    def hello_of(self, p):
        flags = 1 if p.party.host == p.slot else 0
        return bytes([p.slot, flags]) + p.hello

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
            parent = p.party.slots[body[0]] if body[0] < MAX_PLAYERS else None
            self.leave_session(p)
            if parent is None or parent is p:
                self.send(p, 'R')
                return
            p.asked = parent
            self.send(parent, 'C', bytes([p.slot, body[1]]))
        elif kind in 'AR' and len(body) >= 1 and body[0] < MAX_PLAYERS:
            child = p.party.slots[body[0]]
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
        elif kind == 'N' and len(body) >= 3:
            p.hello = body[:3] + body[3:3 + 40]
            for q in p.party.slots:
                if q:
                    self.send(q, 'N', self.hello_of(p))
        elif kind == 'S' and len(body) >= 4:
            p.status = body[:6]
            for q in self.others(p):
                self.send(q, 'S', bytes([p.slot]) + p.status)
        elif kind == 'E' and len(body) >= 3:
            for q in self.others(p):
                self.send(q, 'E', bytes([p.slot]) + body[:3])
        elif kind == 'Q':
            self.send(p, 'q', body[:4])
        elif kind == 'M' and len(body) >= 1 and p.party.host == p.slot:
            p.party.max = max(2, min(MAX_PLAYERS, body[0]))
            for q in p.party.slots:
                if q:
                    self.send(q, 'M', bytes([p.party.max]))

    def enter(self, kind, body, writer):
        """A J or H message from a new connection: its Player, or None if it was turned away."""
        if kind == 'H':
            party = Party(self.new_code(), max(2, min(MAX_PLAYERS, body[0] if body else MAX_PLAYERS)))
            self.parties[party.name] = party
            party.host = 0
        else:
            name = body[:32].decode('utf-8', 'replace').strip()
            # Codes are typed by hand: any case, with or without the dash.
            key = name.upper()
            if len(key) == 7 and key.startswith('SSR') and '-' not in key:
                key = 'SSR-' + key[3:]
            party = self.parties.get(key) or self.parties.get(name)
            if party is None:
                if key.startswith('SSR-'):
                    writer.write(b'\x01\x00F')
                    return None
                party = self.parties[name] = Party(name)
        used = sum(1 for q in party.slots if q)
        if used >= party.max:
            writer.write(b'\x01\x00F')
            return None
        slot = party.slots.index(None)
        p = party.slots[slot] = Player(party, slot, writer)
        writer.write(b'\x02\x00I' + bytes([slot]))
        self.send(p, 'K', party.name.encode())
        self.send(p, 'M', bytes([party.max]))
        for q in self.others(p):
            if q.beacon is not None:
                self.send(p, 'B', bytes([q.slot]) + q.beacon)
            if q.hello is not None:
                self.send(p, 'N', self.hello_of(q))
            if q.status is not None:
                self.send(p, 'S', bytes([q.slot]) + q.status)
            self.send(q, 'j', bytes([slot]))
        print(f'player {slot + 1} joined party {party.name!r}', flush=True)
        return p

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
                    if kind not in 'JH':
                        return
                    p = self.enter(kind, body, writer)
                    if p is None:
                        return
                    continue
                self.message(p, kind, body)
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            if p:
                party = p.party
                self.leave_session(p)
                for q in self.others(p):
                    self.send(q, 'b', bytes([p.slot]))
                    self.send(q, 'l', bytes([p.slot]))
                party.slots[p.slot] = None
                if not any(party.slots):
                    del self.parties[party.name]
                print(f'player {p.slot + 1} left party {party.name!r}', flush=True)
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
