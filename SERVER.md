# Hosting Johto Online

Two small Python services, both standard library only (Python 3.9 or newer), in `server/`:

| Service | What it does | Needed? |
|---|---|---|
| `server/relay_service.py` | The party relay (`tools/party_server.py`) with limits, a health check and optional account checks | Yes, to play online |
| `server/account_server.py` | Sign-in with Discord, signed tokens, SQLite | No. Without it everyone plays as before |

The relay never needs the account server. Turn accounts on only when you want to know who is playing, ban people or limit play to testers.

## 1. The relay

Players enter its address in the game: pause menu, Online tab, Server, for example `play.example.org` (port 7373 is the default) or `play.example.org:7373`. Everyone who should meet uses the same address.

**Open this port** in the firewall and on your router: TCP 7373. The health port (7374) stays closed to the internet.

### Docker

    cp server/.env.example server/.env      # or create it: see the variables below (never commit it)
    docker compose -f server/docker-compose.yml up -d --build
    docker compose -f server/docker-compose.yml --profile accounts up -d   # also the account server

Stop with `docker compose ... down`: the relay closes cleanly on SIGTERM. The image has a health check.

### Linux without Docker (systemd)

    sudo useradd --system johto
    sudo cp -r . /opt/johto-online          # the repository (only tools/party_server.py and server/ are used)
    sudo mkdir /etc/johto-online            # put RELAY_* settings in relay.env, one NAME=value per line
    sudo cp server/johto-relay.service /etc/systemd/system/
    sudo systemctl enable --now johto-relay

Logs: `journalctl -u johto-relay -f`. Health: `curl http://127.0.0.1:7374/health`.

### Windows

Task Scheduler: Create Task, "Run whether user is logged on or not", trigger "At startup", action
`python.exe` with arguments `server\relay_service.py` and "Start in" the repository folder. Set the
settings as system environment variables, or use a small `.bat` that sets them first. Or with NSSM:

    nssm install JohtoRelay "C:\Path\To\python.exe" server\relay_service.py
    nssm set JohtoRelay AppDirectory C:\johto-online
    nssm set JohtoRelay AppEnvironmentExtra RELAY_MAX_SESSIONS=50
    nssm start JohtoRelay

Allow TCP 7373 in Windows Firewall.

### Settings (environment variables)

| Variable | Default | Meaning |
|---|---|---|
| `RELAY_PORT` | 7373 | Game port |
| `RELAY_HEALTH_PORT` | 7374 | `GET /health` (JSON: sessions, players, connections, uptime); 0 turns it off |
| `RELAY_MAX_CONNECTIONS` | 200 | All connections at once |
| `RELAY_MAX_PER_IP` | 8 | Connections from one address (players sharing a home network count together) |
| `RELAY_MAX_SESSIONS` | 50 | Sessions at once; joining an existing one still works |
| `RELAY_IDLE_SECONDS` | 120 | Closes a connection that is silent this long (the game pings every few seconds) |
| `RELAY_REQUIRE_ACCOUNT` | 0 | 1: only signed-in players get in |
| `ACCOUNT_SECRET` | | The account server's secret, to check tokens |
| `ACCOUNT_URL` | | Optional. If set, the relay asks the account server too, so a ban works at once (otherwise a banned player's token works until it expires) |

A session holds up to 4 players: that is the game's own limit, not a setting.

### Behind a domain, and TLS

The game speaks plain TCP to the relay; the game does not do TLS to it. Point a DNS A record (for example `play.example.org`) at the machine and forward TCP 7373. A proxy (Cloudflare, for example) must be in TCP mode, not HTTP. Game traffic is not secret, but if you require accounts, the sign-in token travels in the clear on this connection: it is valid for 30 days, so a sniffed one could be replayed. Keep that in mind on shared networks, or use a VPN.

## 2. The account server

### Create the Discord application

1. Go to https://discord.com/developers/applications and create an application.
2. OAuth2: copy the **Client ID** and **Client Secret**. Under Redirects add `https://accounts.example.org/callback` (exactly `PUBLIC_URL` + `/callback`).
3. Optional, for a beta role: create a bot in the same application, copy its token, invite it to your server (no special permissions are needed), turn on the **Server Members Intent**, and copy the server (guild) ID and the role ID (Discord developer mode: right-click, Copy ID).

### Settings

| Variable | Meaning |
|---|---|
| `ACCOUNT_SECRET` | Required, 16+ random characters (`python -c "import secrets; print(secrets.token_urlsafe(32))"`). Signs tokens. Give the relay the same value. Changing it signs everyone out |
| `PUBLIC_URL` | Required, e.g. `https://accounts.example.org` |
| `DISCORD_CLIENT_ID`, `DISCORD_CLIENT_SECRET` | From the Discord application |
| `DISCORD_REDIRECT_URI` | Default `PUBLIC_URL/callback` |
| `DISCORD_BOT_TOKEN`, `DISCORD_GUILD_ID`, `DISCORD_BETA_ROLE_ID` | All three, or none. Only members with the role may sign in |
| `ACCOUNT_DB` | SQLite file, default `accounts.db` (back it up) |
| `ACCOUNT_HOST`, `ACCOUNT_PORT` | Default `127.0.0.1` and 7380 |
| `TRUST_PROXY` | 1 when behind your own reverse proxy: rate limits use `X-Forwarded-For` |
| `TOKEN_DAYS` | Token life, default 30 |

Run it: `python3 server/account_server.py serve` (or the Docker `accounts` profile, or `server/johto-accounts.service`, with the variables in `/etc/johto-online/accounts.env`).

### TLS: a reverse proxy

Discord needs an https redirect, so put a proxy in front. Caddy (certificates by themselves): see `server/Caddyfile.example`. nginx: `server/nginx-accounts.conf.example`. Open TCP 443 (and 80 for certificates). Keep 7380 closed.

### Banning

    python3 server/account_server.py ban DISCORD_ID      # also: unban, list

With `ACCOUNT_URL` set on the relay, a ban takes effect within a minute for players already holding a token.

### How players sign in

Online tab, Account, press A (set the account server address first: Online tab, Account server, e.g. `accounts.example.org`). The browser opens on a page showing a short code such as `K7QX-2MPD`; the game shows the same code. If they match, choose "Yes, continue with Discord". When it says signed in, the game picks the token up by itself. The token is kept in the settings file (`account_token`); the name shows in the Profile tab. The game needs `curl` on Linux and macOS (Windows uses its own). Android does not sign in yet.

### Endpoints

`GET /login[?code=]` (redirect, or the code page), `GET /callback`, `POST /device/start`, `POST /device/poll`, `GET /me` (Bearer token), `POST /token/verify`.

### Tables for later

`accounts` (Discord id, name, created, last login, ban and beta flags) is in use. `mail`, `global_box`, `guilds`, `guild_members` exist empty for the mail and gifts, Global Box and guild features; nothing uses them yet.

## Security notes

- Never commit `.env`, `accounts.db` or the secrets; give them to the services through environment variables or files outside the repository.
- Tokens are HMAC-SHA256 signed; whoever knows `ACCOUNT_SECRET` can make tokens. Keep it to the two services.
- The code page exists so nobody can sign you in as someone else by sending a link: only continue if the code equals the one in your game.
- Requests are rate limited per address; the account server never logs query strings.
- Run the services as an ordinary user (the Docker image and the systemd units do).
- The relay is not authenticated by default: anyone who has the address can join. Use `RELAY_REQUIRE_ACCOUNT=1` with a beta role to restrict it.

## Tests

    python -m unittest tests.test_party_server tests.test_relay_service tests.test_account_server

None of them contact Discord or any other outside service.
