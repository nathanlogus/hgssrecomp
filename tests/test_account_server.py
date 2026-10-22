"""server/tokens.py and server/account_server.py: token signing, the device flow, bans, the
beta role. Discord is replaced by a fake; nothing leaves this computer."""
from pathlib import Path
import http.client
import http.server
import json
import sys
import tempfile
import threading
import unittest
import urllib.parse

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'server'))
import account_server
import tokens

SECRET = 'x' * 32


class FakeDiscord:
    def __init__(self):
        self.roles = True

    def exchange(self, code):
        return 'access-' + code

    def user(self, access):
        return {'id': '4242', 'username': 'ash', 'global_name': 'Ash <b>'}

    def has_role(self, uid):
        return self.roles


class TokenTests(unittest.TestCase):
    def test_roundtrip_expiry_tamper(self):
        t = tokens.make(SECRET, '1', 'Ash', days=1, now=1000)
        self.assertEqual(tokens.verify(SECRET, t, now=2000)['sub'], '1')
        self.assertIsNone(tokens.verify(SECRET, t, now=1000 + 86400))
        self.assertIsNone(tokens.verify('y' * 32, t, now=2000))
        head, payload, sig = t.split('.')
        self.assertIsNone(tokens.verify(SECRET, '.'.join([head, payload[:-2] + 'AA', sig]), now=2000))
        for junk in ('', 'a.b', 'v1.x.y', None):
            self.assertIsNone(tokens.verify(SECRET, junk))


class AccountTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.discord = FakeDiscord()
        self.cfg = {'secret': SECRET, 'public': 'http://test', 'db': str(Path(self.dir.name) / 'a.db'),
                    'client_id': 'cid', 'client_secret': 'cs', 'redirect': 'http://test/callback'}
        self.app = account_server.App(self.cfg, self.discord)
        self.srv = http.server.ThreadingHTTPServer(('127.0.0.1', 0), account_server.make_handler(self.app))
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()

    def tearDown(self):
        self.srv.shutdown()
        self.srv.server_close()
        self.app.db.close()
        self.dir.cleanup()

    def call(self, method, path, body=None, bearer=None):
        c = http.client.HTTPConnection('127.0.0.1', self.srv.server_address[1], timeout=5)
        h = {'Authorization': 'Bearer ' + bearer} if bearer else {}
        c.request(method, path, json.dumps(body) if body is not None else None, h)
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, r.getheader('Location'), data

    def start(self):
        return json.loads(self.call('POST', '/device/start')[2])

    def finish(self, d):
        """The browser side: /login, /authorize, then Discord sends the browser back to /callback."""
        self.assertEqual(self.call('GET', '/login?code=' + d['user_code'])[0], 200)
        status, loc, _ = self.call('GET', '/authorize?code=' + d['user_code'])
        self.assertEqual(status, 302)
        q = urllib.parse.parse_qs(urllib.parse.urlsplit(loc).query)
        self.assertEqual(q['client_id'], ['cid'])
        return self.call('GET', '/callback?code=abc&state=' + urllib.parse.quote(q['state'][0]))

    def poll(self, d):
        return json.loads(self.call('POST', '/device/poll', {'device_code': d['device_code']})[2])

    def test_device_flow(self):
        d = self.start()
        self.assertEqual(self.poll(d)['status'], 'pending')
        status, _, page = self.finish(d)
        self.assertEqual(status, 200)
        self.assertIn(d['user_code'].encode(), page)
        self.assertNotIn(b'<b>', page)  # names are escaped
        r = self.poll(d)
        self.assertEqual((r['status'], r['name']), ('ok', 'Ash <b>'))
        self.assertEqual(self.poll(d)['status'], 'expired')  # handed over once
        me = json.loads(self.call('GET', '/me', bearer=r['token'])[2])
        self.assertEqual((me['id'], me['name']), ('4242', 'Ash <b>'))
        v = json.loads(self.call('POST', '/token/verify', {'token': r['token']})[2])
        self.assertTrue(v['valid'])
        self.assertEqual(tokens.verify(SECRET, r['token'])['sub'], '4242')

    def test_ban(self):
        d = self.start()
        self.finish(d)
        tok = self.poll(d)['token']
        self.assertTrue(self.app.set_ban('4242', True))
        self.assertEqual(self.call('GET', '/me', bearer=tok)[0], 403)
        self.assertFalse(json.loads(self.call('POST', '/token/verify', {'token': tok})[2])['valid'])
        d = self.start()  # and signing in again is refused
        self.assertEqual(self.finish(d)[0], 403)
        self.assertEqual(self.poll(d)['status'], 'denied')
        self.app.set_ban('4242', False)
        self.assertEqual(self.call('GET', '/me', bearer=tok)[0], 200)

    def test_beta_role(self):
        self.app.cfg['role'] = 'r'
        self.discord.roles = False
        d = self.start()
        self.assertEqual(self.finish(d)[0], 403)
        self.assertEqual(self.poll(d)['status'], 'denied')
        self.discord.roles = True
        d = self.start()
        self.assertEqual(self.finish(d)[0], 200)

    def test_bad_requests(self):
        self.assertEqual(self.call('GET', '/me')[0], 401)
        self.assertEqual(self.call('GET', '/me', bearer='v1.a.b')[0], 401)
        self.assertEqual(self.call('GET', '/callback?code=a&state=forged')[0], 400)
        self.assertEqual(self.call('GET', '/login?code=NOPE-NOPE')[0], 410)
        self.assertEqual(self.call('GET', '/nothing')[0], 404)
        self.assertEqual(self.call('GET', '/device/start')[0], 404)
        self.assertEqual(self.call('GET', '/login')[1], '/authorize')

    def test_stub_tables_exist(self):
        for t in ('mail', 'global_box', 'guilds', 'guild_members'):
            self.assertEqual(self.app.q(f'SELECT count(*) AS n FROM {t}')[0]['n'], 0)


if __name__ == '__main__':
    unittest.main()
