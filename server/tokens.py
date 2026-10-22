"""Signed session tokens shared by the account server and the relay.

Format: v1.<payload>.<signature>, both base64url; the payload is JSON {"sub": Discord id,
"name": display name, "iat": ..., "exp": ...}, the signature HMAC-SHA256 over "v1.<payload>"
with the secret both services share (ACCOUNT_SECRET). Only the standard library.
"""
import base64
import hashlib
import hmac
import json
import time

DAY = 86400


def _b64(b):
    return base64.urlsafe_b64encode(b).rstrip(b'=').decode()


def _unb64(s):
    return base64.urlsafe_b64decode(s + '=' * (-len(s) % 4))


def _sign(secret, text):
    return _b64(hmac.new(secret.encode(), text.encode(), hashlib.sha256).digest())


def make(secret, sub, name, days=30, now=None):
    now = int(now if now is not None else time.time())
    body = 'v1.' + _b64(json.dumps({'sub': sub, 'name': name, 'iat': now, 'exp': now + days * DAY},
                                   separators=(',', ':')).encode())
    return body + '.' + _sign(secret, body)


def verify(secret, token, now=None):
    """The payload dict if the token is genuine and not expired, else None."""
    try:
        head, payload, sig = token.split('.')
        if head != 'v1' or not hmac.compare_digest(sig, _sign(secret, f'{head}.{payload}')):
            return None
        data = json.loads(_unb64(payload))
        if data['exp'] <= (now if now is not None else time.time()):
            return None
        return data
    except (ValueError, KeyError, TypeError, AttributeError):
        return None
