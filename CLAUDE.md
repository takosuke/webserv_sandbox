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

`EpollLoop` is a singleton that owns all active network connections as `map<int, Connection*>` (fd → Connection). It calls `epoll_wait()` with a 5 ms timeout, dispatches each ready event to `connection->handle(events)`, then flushes its `_deletion_queue` (connections are queued for deletion during event handling rather than deleted immediately to avoid dangling pointers within the same batch).

SIGINT triggers a `sig_int` flag that stops the loop cleanly.

- `add(conn)` — register fd with `EPOLLIN | EPOLLERR | EPOLLHUP`
- `mod(conn, events)` — replace the watched event mask (pass `0` to pause a connection while awaiting file I/O)
- `del(conn)` — enqueue for deletion; deregistered, closed, and freed at end of tick

### Connection Hierarchy

```
Connection (abstract, owns fd + Http* config)
├── ServerConnection  — listens; accept() → new ClientConnection → epoll.add()
├── ClientConnection  — reads request bytes; builds and writes response
└── CgiConnection     — reads CGI output from pipe; calls back into ClientConnection
```

`ClientConnection` holds a `RequestParser`, a `Response`, and a fixed 1024-byte `_buffer`. On a complete request it checks whether the matched `Location` has a `cgi_pass`; if so it calls `handle_cgi()`, otherwise dispatches by HTTP method.

**GET flow:**
1. Constructs `Response(_buffer, 1024, root + path)` — opens the file via `std::fstream` inside the constructor
2. Builds headers, then mods itself to `EPOLLOUT`
3. `handle(EPOLLOUT)` calls `_response.write_to(fd)`, which fills the buffer from `fstream` and drains it to the socket; repeats until `done()` or `error()`, then `del(this)`

**CGI flow:**
1. `fork()` + `pipe()` + `execve()` the interpreter with CGI env vars (`GATEWAY_INTERFACE`, `REQUEST_METHOD`, `SCRIPT_FILENAME`, `QUERY_STRING`, etc.) plus any `cgi_param` pairs
2. Creates `CgiConnection(this)` with the read end of the pipe, sets `_pid`
3. `epoll.mod(this, 0)` + `epoll.add(cgiConn)`
4. `CgiConnection::handle(EPOLLIN)` accumulates pipe output into `_output`; when the pipe closes, `waitpid()` and calls `callback->complete_cgi(_output)`
5. `complete_cgi()` splits CGI headers from body at `\r\n\r\n`, writes body to a temp file, builds `Response` from CGI headers + tempfile, then `epoll.mod(clientConn, EPOLLOUT)`

### Request Parsing

`RequestParser` is a state machine with states `REQUEST_LINE → HEADERS → BODY → COMPLETE`. It is constructed with the `Http*` config and the local `sockaddr_in` so it can resolve the matching `Server` and `Location` during parsing and store pointers directly on the `Request`.

`Request` fields:
- `method`, `uri`, `path` (decoded), `query`, `version`
- `headers` (map with lowercased keys), `body`, `content_length`
- `host`, `hostname`, `port` (extracted from `Host` header)
- `server`, `location` — pointers into the config tree set by the parser
- `valid`, `error` (HTTP status code on parse failure)

### Response

`Response` owns a caller-supplied `char *buffer` of fixed `capacity`, a `std::vector<std::string> headers` queue, and a `std::fstream stream` for file content. `write_to(fd)` fills the buffer from pending headers then the file stream, writes as much as possible to `fd`, and tracks position with `pos`/`size`. `done()` returns true when headers are empty, the stream is exhausted, and the buffer is drained. `error()` reflects stream or write failures.

Key builder methods: `add_status_line()`, `add_header_field()`, `add_content_length()` (uses `stat()` on the file), `add_date()`, `add_header_end()`. Call `set_file()` to attach a file after construction. `set_internal_error()` replaces the buffer with a static 500 response. `construct_3xx()` builds a redirect response inline.

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
| `cgi_pass <interpreter>` | location | interpreter path; enables CGI for the location |
| `cgi_param <key> <value>` | location | extra env vars passed to the CGI process |

### Logger

`Logger` is a singleton with levels `LOG_DEBUG < LOG_INFO < LOG_WARN < LOG_ERROR`. Set at startup via the `LOG_LEVEL` env var (`DEBUG`, `INFO`, `WARN`, `ERROR`); defaults to `LOG_DEBUG`. Use via macros:

```cpp
LOG_INFO("component") << "message" << std::endl;
```

### Data Flow

1. `main()` parses config → builds `Http` → creates one `ServerConnection` per `Port` entry → enters `EpollLoop::run()`
2. Accept: `ServerConnection::handle()` → `accept()` → `new ClientConnection(fd, &http, addr)` → `epoll.add()`
3. Read: `ClientConnection::handle(EPOLLIN)` → `read()` → `RequestParser::feed()` → on complete: check for CGI, else dispatch by method
4. GET: `handle_get()` constructs `Response` (opens file via fstream), builds headers, `epoll.mod(this, EPOLLOUT)`
5. CGI: `handle_cgi()` forks child, `epoll.mod(this, 0)`, `new CgiConnection` → `epoll.add(cgiConn)`; when pipe drains `complete_cgi()` → `epoll.mod(this, EPOLLOUT)`
6. Write: `ClientConnection::handle(EPOLLOUT)` → `Response::write_to(fd)` → on `done()` or `error()`: `epoll.del(this)`
7. Error/hangup at any stage: `epoll.del(conn)` → connection closed and freed at end of tick

### Source Layout

- `src/` — `webserv.cpp`, `EpollLoop`, `ServerConnection`, `ClientConnection`, `CgiConnection`, `Request`, `RequestParser`, `Response`, `Config`, `ConfigParser`, `utils`; also `ServerBlock` (currently unused legacy stub)
- `inc/` — headers for the above, plus `Logger.hpp`
- `parser/src/` — tokenizer (`Lexer`, `*Lex` classes), directive tree (`Grouper`, `Directive`), config object construction
- `parser/inc/` — `config.hpp` (structs: `listen`, `mime`, `errors`, `limit`, etc.), `Http.hpp`, `Server.hpp`, `Location.hpp`, `Port.hpp`
- `incremental_versions/` — historical snapshots, not compiled by make
- `tests/` — currently empty
