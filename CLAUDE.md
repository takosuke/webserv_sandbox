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

`EpollLoop` is a singleton that owns all active network connections as `map<int, Connection*>` (fd → Connection). It calls `epoll_wait()` with a 5 ms timeout and dispatches each ready event to `connection->handle(events)`. After dispatching, it calls `FileLoop::get_instance().run()` to process any pending file I/O, then flushes its `_deletion_queue` (connections are queued for deletion during event handling rather than deleted immediately to avoid dangling pointers within the same batch).

SIGINT triggers a `sig_int` flag that stops the loop cleanly.

- `add(conn)` — register fd with `EPOLLIN | EPOLLERR | EPOLLHUP`
- `mod(conn, events)` — replace the watched event mask (pass `0` to pause a connection while awaiting file I/O)
- `del(conn)` — enqueue for deletion; deregistered, closed, and freed at end of tick

### Connection Hierarchy

```
Connection (abstract, owns fd + Http* config)
├── ServerConnection  — listens; accept() → new ClientConnection → epoll.add()
└── ClientConnection  — reads request bytes; drives file I/O; writes response bytes
```

`ClientConnection` holds a `RequestParser`, a `Response`, and an optional `FileConnection*` for in-flight file reads. On a complete GET request it:
1. Opens the file, constructs a `ReadFileConnection` pointing at `_response.entity` as the output buffer
2. Suspends its own epoll events with `mod(this, 0)`
3. Registers the `FileConnection` with `FileLoop`
4. When the file finishes, `FileConnection::handle_callback()` re-arms the `ClientConnection` with `EPOLLOUT`
5. `handle(EPOLLOUT)` drains the `Response` via `Response::write_count()`, then `del(this)`

### File I/O

`FileLoop` is a synchronous singleton separate from epoll. On each epoll tick, `EpollLoop::run()` calls `FileLoop::run()`, which iterates all registered `FileConnection*` and calls `handle()` on each. A `ReadFileConnection` reads up to `BYTES_PER_READ_CYCLE` (1024) bytes from the file fd into the target string buffer per call; a `WriteFileConnection` does the reverse. When done, `handle_callback()` re-arms the parent `ClientConnection` and queues itself for deletion.

### Request Parsing

`RequestParser` is a state machine with states `REQUEST_LINE → HEADERS → BODY → COMPLETE`. It is constructed with the `Http*` config and the local `sockaddr_in` so it can resolve the matching `Server` and `Location` during parsing and store pointers directly on the `Request`.

`Request` fields:
- `method`, `uri`, `path` (decoded), `query`, `version`
- `headers` (map with lowercased keys), `body`, `content_length`
- `host`, `hostname`, `port` (extracted from `Host` header)
- `server`, `location` — pointers into the config tree set by the parser
- `valid`, `error` (HTTP status code on parse failure)

### Response

`Response` stores `status_line`, `headers`, and `entity` as separate strings and writes them sequentially across calls via `write_count(fd, count)`, tracking position internally. `construct_status_line()` and `add_header_field()` build the response; `add_content_length()` appends the length of `entity`.

### Configuration

`inc/Config.hpp` defines the full config object tree: `Http`, `Server`, `Location`, `Port`, and all `config::*` structs. The parser submodule (`parser/`, builds `libparser.a`) tokenizes an nginx-like config file and populates a `Grouper`; `Http(grouper.main.body_directives[0])` constructs the tree from it. `inc/ConfigParser.hpp` provides the `Grouper` and related types.

Config hierarchy: `http {}` → `server {}` → `location {}`. Directives at outer scopes are inherited. Virtual host resolution: `Http::get_server(sockaddr_in, hostname)` looks up by listen address then by `server_name` via the `Port` map, falling back to the default server.

Key directives:

| Directive | Scope | Notes |
|---|---|---|
| `listen <ip>:<port> [default_server] [backlog=N]` | server | multiple allowed |
| `server_name <name>...` | server | |
| `root <path>` | http/server/location | |
| `error_page <code>... <uri>` | http/server/location | optional response-code override and CGI flag |
| `client_max_body_size <size>` | http/server/location | k/m suffixes |
| `client_header_buffer_size`, `large_client_header_buffers` | http/server | |
| `client_body_buffer_size`, `client_body_timeout` | http/server/location | |
| `output_buffers` | http/server/location | |
| `types { <mime> <ext>...; }` | http/server/location | |
| `default_type <mime>` | http/server/location | |
| `limit_except <method>... { ... }` | location | |

### Logger

`Logger` is a singleton with levels `LOG_DEBUG < LOG_INFO < LOG_WARN < LOG_ERROR`. Set at startup via the `LOG_LEVEL` env var (`DEBUG`, `INFO`, `WARN`, `ERROR`); defaults to `LOG_DEBUG`. Use via macros:

```cpp
LOG_INFO("component") << "message" << std::endl;
```

### Data Flow

1. `main()` parses config → builds `Http` → creates one `ServerConnection` per `Port` entry → enters `EpollLoop::run()`
2. Accept: `ServerConnection::handle()` → `accept()` → `new ClientConnection(fd, &http, addr)` → `epoll.add()`
3. Read: `ClientConnection::handle(EPOLLIN)` → `read()` → `RequestParser::feed()` → on complete: dispatch by method
4. GET: open file → `new ReadFileConnection` → `epoll.mod(this, 0)` → `FileLoop.add(fileConn)`
5. File done: `handle_callback()` → `epoll.mod(clientConn, EPOLLOUT)`
6. Write: `ClientConnection::handle(EPOLLOUT)` → `Response::write_count()` → on finish: `epoll.del(this)`
7. Error/hangup at any stage: `epoll.del(conn)` → connection closed and freed at end of tick

### Source Layout

- `src/` — `webserv.cpp`, `EpollLoop`, `FileLoop`, `ServerConnection`, `ClientConnection`, `FileConnection`, `Request`, `RequestParser`, `Response`, `Config`, `ConfigParser`, `utils`
- `inc/` — headers for the above, plus `Logger.hpp`
- `parser/src/` — tokenizer (`Lexer`, `*Lex` classes), directive tree (`Grouper`, `Directive`), config object construction
- `parser/inc/` — `config.hpp` (structs: `listen`, `mime`, `errors`, `limit`, etc.), `Http.hpp`, `Server.hpp`, `Location.hpp`, `Port.hpp`
- `incremental_versions/` — historical snapshots, not compiled by make
- `tests/` — currently empty
