# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make          # Build ./webserv (release, -O3)
make DEBUG=1  # Build with -g3 -fno-limit-debug-info (no -O3)
make re       # Full rebuild
make clean    # Remove obj/
make fclean   # Remove obj/, webserv, parser/libparser.a
```

The parser submodule is built automatically as part of `make` (`$(MAKE) -C parser lib`). To build and run the parser standalone for config testing:

```bash
cd parser && make && ./parser ../webserv.conf
```

Run the server:

```bash
./webserv webserv.conf
```

## Constraints

- **C++98 only** — no C++11 or later. No lambdas, no `auto`, no range-for, no `nullptr`.
- **`-Wall -Werror -Wextra`** — all warnings are errors. Every new file must compile clean.
- **Linux only** — uses `epoll`, not `poll` or `select`.

## Architecture

### Event Loop

`EpollLoop` owns all active connections as `map<int, Connection*>` (fd → Connection). It calls `epoll_wait()` in a blocking loop and dispatches each event to `connection->handle(events)`.

- `add(conn)` — register fd with `EPOLLIN | EPOLLERR | EPOLLHUP`
- `mod(conn, events)` — change the watched event mask (e.g., add `EPOLLOUT` when there is data to write)
- `del(conn)` — deregister, `close(fd)`, `delete` the connection

### Connection Hierarchy

```
Connection (abstract, owns fd + Http* config)
├── ServerConnection  — listens; accept() → new ClientConnection → epoll.add()
└── ClientConnection  — reads request bytes; writes response bytes
```

`ClientConnection` uses `RequestParser` to accumulate bytes and detect request completion (currently: finding `\r\n\r\n`). When a full request is received it enqueues a response into `_write_buf`, switches to `EPOLLOUT`, and drains the buffer on subsequent write events.

### Configuration

The `parser/` directory is a git submodule (`ws_parser`) that builds into `libparser.a`. It tokenizes an nginx-like config file and produces a `Http` object.

Config hierarchy: `http {}` → `server {}` → `location {}`. Directives at outer scopes are inherited by inner scopes. Key directives:

| Directive | Scope | Notes |
|---|---|---|
| `listen <ip>:<port> [default_server] [backlog=N]` | server | multiple allowed |
| `server_name <name>...` | server | |
| `root <path>` | http/server/location | |
| `error_page <code>... <file>` | http/server/location | |
| `client_max_body_size <size>` | http | supports k/m suffixes |
| `types { <mime> <ext>...; }` | http | |
| `limit_except <method>... { ... }` | location | |

### Data Flow

1. `main()` parses config → builds `Http` → creates one `ServerConnection` per listen address → enters `EpollLoop::run()`
2. Accept: `ServerConnection::handle()` → `accept()` → `new ClientConnection` → `epoll.add()`
3. Read: `ClientConnection::handle(EPOLLIN)` → `read()` → `RequestParser::feed()` → detect complete request
4. Write: switch to `EPOLLOUT` → `ClientConnection::handle(EPOLLOUT)` → drain `_write_buf` → switch back to `EPOLLIN`
5. Error/hangup: `epoll.del()` → connection closed and freed

### Source Layout

- `src/` — main server sources (`webserv.cpp`, `EpollLoop`, `ServerConnection`, `ClientConnection`, `Request`, `RequestParser`, `utils`)
- `inc/` — headers for the above
- `parser/src/` — tokenizer (`Lexer`, `*Lex` classes), directive tree (`Grouper`, `Directive`), config objects (`Http`, `Server`, `Location`, `Port`)
- `parser/inc/` — parser headers including `config.hpp` (structs: `listen`, `mime`, `errors`, `limit`, etc.)
- `incremental_versions/` — historical snapshots, not compiled by make
- `tests/` — currently empty
