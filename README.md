# Production-Ready CLI Chat Application

A secure, scalable TCP-based chat application written in C++17 using standalone Asio for async networking and OpenSSL for TLS encryption.

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
- **Room Manager**: Chat room membership and message broadcasting
- **Storage**: SQLite for development, PostgreSQL-ready interface
- **Health Endpoint**: HTTP health check for load balancers/orchestrators

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
    "type": "message | command | system | error | presence | auth_request | auth_response | room_event | direct_message",
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
| `message` | Chat message in a room |
| `command` | Slash command from client |
| `system` | System notification |
| `error` | Error response |
| `presence` | User online/offline/join/leave |
| `auth_request` | Login/register request |
| `auth_response` | Authentication result |
| `room_event` | Room join/leave confirmation |
| `direct_message` | Private message |

## Building

### Prerequisites

- CMake 3.16+
- C++17 compiler (GCC 9+, Clang 10+)
- OpenSSL 1.1+
- SQLite3
- libsodium

### Ubuntu/Debian

```bash
# Install dependencies
sudo apt-get update
sudo apt-get install -y \
    build-essential cmake git \
    libssl-dev \
    libsqlite3-dev \
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
| `ENABLE_REDIS` | OFF | Enable Redis Pub/Sub support |

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
| `/rename <newname>` | Change username |
| `/quit` | Disconnect from server |
| `/help` | Show help message |

### Example Session

```
$ ./chat_client localhost 8443
Connecting to localhost:8443...
Connected! Type /help for available commands.

> /register alice mypassword123
✓ Registration successful! You can now login with /login

> /login alice mypassword123
✓ Welcome back, alice!
Joined #general - Users: alice

alice #general> Hello everyone!
[14:30:45] #general alice: Hello everyone!

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
    "redis_enabled": false,
    "redis_host": "localhost",
    "redis_port": 6379,
    "rate_limit_messages_per_second": 10,
    "rate_limit_burst": 20,
    "log_level": "info",
    "log_file": ""
}
```

## Security Features

- **TLS 1.2+**: All connections encrypted with modern cipher suites
- **Argon2id**: Password hashing using libsodium's implementation
- **Rate Limiting**: Token bucket algorithm per-IP and per-user
- **Message Validation**: Size limits and content validation
- **No Global State**: Thread-safe design with proper synchronization

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

## Next Steps for Production

1. **Implement PostgreSQL storage** for production data persistence
2. **Complete Redis Pub/Sub** integration for horizontal scaling
3. **Add metrics endpoint** (Prometheus format)
4. **Implement message history** loading on room join
5. **Add file upload support** with S3/MinIO backend
6. **Implement user roles** and permissions
7. **Add audit logging** for compliance
8. **Set up TLS certificate rotation**

## License

MIT License - See LICENSE file for details.

