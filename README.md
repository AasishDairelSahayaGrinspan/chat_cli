# CLI Chat Application

A secure, scalable TCP-based chat application written in C++17 using standalone Asio for async networking and OpenSSL for TLS encryption.

## What Is Implemented Today

- **Secure transport**: TLS 1.2+ client/server connections with optional custom CA and dev-only insecure mode
- **Authentication and identity**: Register/login, username validation, rename, duplicate-login prevention
- **Room-based chat**: Join/leave rooms, list rooms/users, broadcast within room
- **End-to-end encrypted rooms (protocol v2)**: Sender-key ratchet with Ed25519 signatures, XChaCha20-Poly1305 payloads, per-epoch key rotation on membership change, TOFU identity pinning, replay/skipped-key handling — the server only relays ciphertext
- **Direct messaging**: Private DMs with end-to-end encrypted payload support and TOFU key pinning
- **Key verification**: `/verify <user>` fingerprint comparison (out-of-band) and `/roomkeys` per-room key state, with automatic warnings on identity key changes
- **Modern terminal UI**: FTXUI interface with animated banner, message feed, status bar, and command completion
- **Client resilience**: Automatic reconnect attempts on disconnect
- **Server safety controls**: Per-IP/per-user token-bucket rate limiting, input sanitization, login lockout window
- **Persistence (SQLite)**: Users, rooms, messages, queued room key envelopes, and E2EE public keys stored in SQLite
- **Operational readiness**: HTTP liveness/readiness endpoints for orchestration and load balancers
- **Test suite**: Crypto, trust-store, and end-to-end group-flow tests (`BUILD_TESTS=ON`) with CI on Linux and macOS

## Current Limitations

- **PostgreSQL storage**: Interface exists but production implementation is not completed yet
- **Redis scaling path**: Broker abstraction exists; full Redis pub/sub wiring is not completed yet
- **Message history UX**: Messages are persisted server-side (room ciphertext is stored verbatim and stays opaque to the server), but no client `/history` command yet
- **Protocol v2 required for room chat**: On servers with `room_e2ee_required: true` (default), pre-v2 clients cannot read room messages (they are rejected when sending plaintext and show `[unable to decrypt]` for encrypted ones)

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Chat Server                             │
├─────────────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐           │
│  │  TLS Server  │  │   Session    │  │    Health    │           │
│  │  (Acceptor)  │  │   Manager    │  │   Endpoint   │           │
│  └──────┬───────┘  └──────┬───────┘  └──────────────┘           │
│         │                 │                                     │
│  ┌──────┴───────┐  ┌──────┴───────┐  ┌──────────────┐           │
│  │  Connection  │  │     Rate     │  │    Command   │           │
│  │   Manager    │  │   Limiter    │  │   Handler    │           │
│  └──────────────┘  └──────────────┘  └──────┬───────┘           │
│                                             │                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────┴───────┐           │
│  │     Auth     │  │     Room     │  │   Storage    │           │
│  │   Service    │  │   Manager    │  │  (SQLite/PG) │           │
│  └──────────────┘  └──────────────┘  └──────────────┘           │
│                                                                 │
│  ┌──────────────────────────────────────────────────┐           │
│  │              Redis Pub/Sub (Optional)             │          │
│  │           For Horizontal Scaling                  │          │
│  └──────────────────────────────────────────────────┘           │
└─────────────────────────────────────────────────────────────────┘
```

### Component Responsibilities

- **TLS Server**: Accepts incoming SSL/TLS connections using standalone Asio
- **Connection Manager**: Tracks active connections, handles broadcasting
- **Session Manager**: Manages user sessions and authentication state
- **Rate Limiter**: Token bucket rate limiting per-IP and per-user
- **Command Handler**: Processes slash commands and chat messages
- **Auth Service**: User registration and login with Argon2id password hashing
- **Room Manager**: Chat room membership, epoch rotation, and message broadcasting
- **Storage**: SQLite for development, PostgreSQL-ready interface
- **Health Endpoint**: HTTP health check for load balancers/orchestrators

On the client side, a **Group Manager** owns room sender keys, epochs, TOFU
trust pins, and envelope processing; see
[End-to-End Encryption](#end-to-end-encryption).

## Protocol Specification

### Frame Format

All messages use a length-prefixed binary frame:

```
┌─────────────────┬────────────────────────────────────┐
│  Length (4B)    │         JSON Payload (UTF-8)       │
│  Big-endian     │                                    │
└─────────────────┴────────────────────────────────────┘
```

### Message Structure

```json
{
    "type": "message | command | system | error | presence | auth_request | auth_response | room_event | direct_message | key_exchange | room_key | room_membership | encrypted_message",
    "id": "uuid-v4",
    "timestamp": 1709312400,
    "from": "username",
    "room": "room_name",
    "payload": { ... }
}
```

### Message Types

| Type | Description |
|------|-------------|
| `message` | Chat message in a room (rejected by v2 servers that require room E2EE) |
| `command` | Slash command from client |
| `system` | System notification |
| `error` | Error response |
| `presence` | User online/offline/join/leave |
| `auth_request` | Login/register request |
| `auth_response` | Authentication result (includes `protocol_version` and `room_e2ee_required`) |
| `room_event` | Room join/leave confirmation (join payload includes `epoch` and `e2ee: true`) |
| `direct_message` | Private message (E2EE payload when keys are available) |
| `key_exchange` | Public key publish/lookup (`publish_key`, `request_key` / `key_response`) |
| `room_key` | Pairwise-wrapped room sender key envelope (v2) |
| `room_membership` | Room epoch + member list after join/leave (v2) |
| `encrypted_message` | End-to-end encrypted room message (v2) |

### Protocol Version

Clients send `protocol_version` in the login payload.

- **v1**: plaintext room messages, opportunistic DM encryption.
- **v2**: end-to-end encrypted room messages (sender keys + Ed25519 signatures).

Room chat is only readable by v2 clients. On servers with `room_e2ee_required: true`
(the default), plaintext room messages are rejected with `E2EE_REQUIRED` and v2
clients show `[unable to decrypt]` for messages whose sender key they lack
(e.g. old clients' history, or senders they have never synced with).

## Building

### Prerequisites

- CMake 3.16+
- C++17 compiler (GCC 9+, Clang 10+)
- OpenSSL 1.1+
- SQLite3
- libsodium
- PostgreSQL client library (`libpq`) when building with `ENABLE_POSTGRES=ON`

### Ubuntu/Debian

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake git \
    libssl-dev \
    libsqlite3-dev \
    libpq-dev \
    libsodium-dev \
    pkg-config

# Clone and build
git clone <repository>
cd chat_cli
chmod +x build.sh
./build.sh Release
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_SERVER` | ON | Build the server |
| `BUILD_CLIENT` | ON | Build the client |
| `BUILD_TESTS` | OFF | Build the test suite (`ctest --test-dir build`) |
| `ENABLE_REDIS` | OFF | Enable Redis Pub/Sub support |
| `ENABLE_POSTGRES` | OFF | Enable PostgreSQL storage backend scaffolding |

To build with PostgreSQL backend enabled:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_POSTGRES=ON
cmake --build build -j
```

## Running

### Generate Certificates (Development)

```bash
cd docker/certs
chmod +x generate.sh
./generate.sh
```

### Start Server

```bash
# Using default config
./build/server/chat_server

# Using custom config
./build/server/chat_server config.json
```

### Start Client

```bash
# Connect to localhost
./build/client/chat_client

# Connect to specific host
./build/client/chat_client example.com 8443
```

### Docker Deployment

```bash
# Generate certificates first
cd docker/certs && ./generate.sh && cd ../..

# Build and run
cd docker
docker-compose up -d

# With Redis for scaling
docker-compose --profile scaling up -d

# With PostgreSQL for production
docker-compose --profile production up -d
```

## Usage

### Slash Commands

| Command | Description |
|---------|-------------|
| `/register <user> <pass>` | Create new account |
| `/login <user> <pass>` | Login to existing account |
| `/join <room>` | Join a chat room |
| `/leave [room]` | Leave current or specified room |
| `/rooms` | List available rooms |
| `/users [room]` | List online users |
| `/dm <user> <message>` | Send private message |
| `/verify <user>` | Show a user's identity fingerprint (compare out-of-band) |
| `/verify <user> accept` | Accept a changed identity key for `<user>` |
| `/roomkeys` | Show room key/epoch state for known rooms |
| `/rename <newname>` | Change username |
| `/quit` | Disconnect from server |
| `/help` | Show help message |

Command aliases are also supported in the client (for example `/j`, `/who`, `/nick`, `/exit`).

For DMs, the client will automatically attempt end-to-end encryption when it has the recipient's cached public key.

Room messages are end-to-end encrypted automatically (protocol v2). When a room's
membership changes, the server bumps the room epoch, clients rotate their sender
keys, and the new keys are distributed to members as pairwise-encrypted
envelopes. See [End-to-End Encryption](#end-to-end-encryption) for the protocol
details.

### Example Session

```
$ ./chat_client localhost 8443
Connecting to localhost:8443...
Connected! Type /help for available commands.

> /register alice mypassword123
✓ Registration successful! You can now login with /login

> /login alice mypassword123
✓ Welcome back, alice!
Joined #general (end-to-end encrypted)

alice #general> Hello everyone!
[14:30:45] #general 🔒 alice: Hello everyone!

alice #general> /verify bob
Identity for 'bob':
  Fingerprint: ab12cd34ef56ab12cd34ef56ab12cd34ef56ab12cd34ef56ab12cd34ef56ab12
  Box key:     GQHGgEK/7xqtTKP33Ew2l+OfBB7KyrCKHiNL2KxmRXc=
  Signing key: rjH2t+ZSjJTPs2QqOvnBAqkTJ52MQ3+s6XEwWMEBF3g=
  Status:      pinned (trust on first use)
Compare the fingerprint with bob out-of-band, then keep chatting.

alice #general> /dm bob Hey, are you there?
[DM -> bob] Hey, are you there?

alice #general> /quit
Goodbye!
```

## Configuration

### Server Configuration (config.json)

```json
{
    "bind_address": "0.0.0.0",
    "port": 8443,
    "health_port": 8080,
    "thread_pool_size": 4,
    "cert_file": "certs/server.crt",
    "key_file": "certs/server.key",
    "dh_file": "certs/dh2048.pem",
    "storage_type": "sqlite",
    "sqlite_path": "chat.db",
    "postgres_conn": "postgresql://chat:password@localhost:5432/chat",
    "redis_enabled": false,
    "redis_host": "localhost",
    "redis_port": 6379,
    "rate_limit_messages_per_second": 10,
    "rate_limit_burst": 20,
    "room_e2ee_required": true,
    "log_level": "info",
    "log_file": ""
}
```

Set `"room_e2ee_required": false` only for legacy deployments that must keep
accepting plaintext room messages from v1 clients. When enabled (default), the
server rejects plaintext room messages with an `E2EE_REQUIRED` error and relays
only `encrypted_message` frames.

## Security Features

- **TLS 1.2+**: All connections encrypted with modern cipher suites
- **Room E2EE**: Sender-key ratchet with Ed25519 signatures — the server only relays ciphertext it cannot read
- **DM E2EE**: X25519 key exchange with XChaCha20-Poly1305 payloads
- **TOFU identity pinning**: Key changes are detected and blocked until manually accepted via `/verify <user> accept`
- **Argon2id**: Password hashing using libsodium's implementation
- **Rate Limiting**: Token bucket algorithm per-IP and per-user
- **Login lockout**: Temporary account lockout after repeated failed login attempts
- **Message Validation**: Size limits and content validation
- **No Global State**: Thread-safe design with proper synchronization

## End-to-End Encryption

Room messages are end-to-end encrypted between clients; the server relays and
stores opaque ciphertext. The design favors the sender-keys pattern used by
modern group messengers.

### Threat Model

| Adversary | Property |
|-----------|----------|
| Server / storage compromise | Cannot read message contents or forge messages as a user (payloads are E2EE, signatures cover sender identity) |
| Passive network attacker | Sees only TLS + ciphertext |
| Removed room member | Cannot decrypt messages sent in later epochs (keys rotate on leave) |
| Message tampering / replay | Rejected: AEAD with room/sender/epoch/key-id/index as AAD, Ed25519 signature over the same, one-time message keys, and per-sender index dedup |
| Active attacker swapping identity keys | Detected via TOFU pinning; a changed key for a known user is refused until `/verify <user> accept` |

Not protected against: a compromised endpoint (malware with access to the key
directory), or users who never compare fingerprints out-of-band (TOFU is trust
on first use).

### Primitives

- **X25519** (`crypto_box`) for pairwise key envelopes and DMs
- **Ed25519** for detached signatures over room envelopes and messages
- **XChaCha20-Poly1305** for message and envelope encryption
- **BLAKE2b** for fingerprints and the symmetric-key ratchet
- **libsodium** for all primitives and constant-time helpers

### Room Key Protocol (v2)

1. Every v2 client has a long-term X25519 box keypair and an Ed25519 signing
   keypair, generated on first run under `~/.chat_cli/keys/` (`0600`).
2. On login the client publishes its X25519 public key to the server's key
   directory (`key_exchange`), and exchanges lookups on demand.
3. The server assigns each room an **epoch**, bumped on every join/leave, and
   broadcasts `room_membership` with the member list.
4. On epoch change each client rotates its **sender key**: a chain key derived
   with `mk = BLAKE2b(0x01 ‖ ck)`, `ck' = BLAKE2b(0x02 ‖ ck)`. Message keys are
   used at most once, so a leaked message key does not expose others.
5. The sender key is distributed to each member wrapped in a pairwise
   `crypto_box` envelope (`room_key`), signed by the sender's Ed25519 key. The
   server queues envelopes for offline members and delivers them on join.
6. Room messages are sent as `encrypted_message` (XChaCha20-Poly1305 with AAD
   binding room, sender, epoch, key id, and message index). The server relays
   them to room members and stores the ciphertext verbatim.
7. Receivers verify the signature, check the TOFU pin, decrypt out-of-order
   messages via a skipped-key cache, and reject replays (each key is consumed
   once). A failed authentication never burns a chain step.

### Verifying Identities

Fingerprints are BLAKE2b hashes of the identity keys. Compare `/verify <user>`
output with your contact through a separate channel; matching fingerprints rule
out a server-side key swap (MITM). If a user's key ever changes, the client
shows a warning and refuses to decrypt their messages until you either
re-pin with `/verify <user> accept` (after out-of-band confirmation) or treat
the change as an attack.

### Breaking Change Notes (Protocol v2)

- Room chat now requires protocol v2 clients. v1 clients can still login and
  DM, but cannot read or send room messages on servers with
  `room_e2ee_required: true`.
- Room message history stored before the upgrade is opaque to v2 clients and
  displays as `[unable to decrypt]`.
- Epoch rotation on join/leave is required: servers that do not announce
  `room_membership` epochs leave members unable to decrypt each other's
  messages.

## Scaling

### Horizontal Scaling with Redis

1. Enable Redis in config: `"redis_enabled": true`
2. Deploy multiple server instances behind a load balancer
3. Messages are published to Redis and broadcast to all instances

### Load Balancer Configuration

- Use TCP load balancing (layer 4)
- Enable sticky sessions if using WebSocket upgrade (not in this implementation)
- Health check endpoint: `GET /healthz` on `health_port`

## Health Checks

- **Liveness**: `GET /healthz` - Returns 200 if server is running
- **Readiness**: `GET /readyz` - Returns 200 if storage is accessible

## Proposed Roadmap

### Phase 1: Production Core

1. **Complete PostgreSQL backend** with migrations, pooling, and parity tests vs SQLite
2. **Finish Redis pub/sub integration** for multi-instance fanout and cross-node presence/DM delivery
3. **Add Prometheus `/metrics` endpoint** for latency, active sessions, auth failures, and message throughput
4. **Add structured audit logging** for auth events, renames, moderation actions, and admin operations

### Phase 2: Chat Experience

1. **Implement `/history` command** with pagination and room join backfill
2. **Add delivery/read receipts for DMs** with protocol fields and UI markers
3. **Add typing indicators and richer presence states** (away, idle, reconnecting)
4. **Add client profile/config file** for default server, CA path, theme, and reconnect policy

### Phase 3: Security and Governance

1. **Add room roles and moderation commands** (`/mute`, `/kick`, `/ban`, room owner/admin)
2. **Implement session/token management improvements** (remember-me token rotation and revoke)
3. **Automate TLS certificate rotation** and hot reload

### Phase 4: Platform Extensions

1. **Add optional file attachments** backed by S3/MinIO with signed URL download flow
2. **Add webhook/event bridge** for bot integrations and notifications
3. **Add admin CLI/API** for room and user lifecycle operations
4. **Add load/perf test suite** and capacity baselines per deployment profile

## License

MIT License - See LICENSE file for details.

