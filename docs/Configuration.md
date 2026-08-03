# Configuration Directives

This document describes every directive understood by the webserv configuration
parser, the contexts it is valid in, its parameters, defaults, and behaviour.

## Table of contents

- [File structure](#file-structure)
- [Value syntax](#value-syntax)
- [Inheritance](#inheritance)
- [Context blocks](#context-blocks)
  - [`http`](#http)
  - [`server`](#server)
  - [`location`](#location)
  - [`types`](#types)
- [Directive reference](#directive-reference)
  - [Connection & networking](#connection--networking)
  - [Request limits & timeouts](#request-limits--timeouts)
  - [Content & routing](#content--routing)
  - [CGI](#cgi)
  - [MIME types](#mime-types)
  - [Error handling](#error-handling)
- [Directive availability matrix](#directive-availability-matrix)

---

## File structure

A configuration file is a tree of nested blocks. The outermost block must be
`http`. Inside it you declare one or more `server` blocks, and inside each
server one or more `location` blocks:

```nginx
http {
    server {
        listen 127.0.0.1:8080 default_server;
        server_name localhost;
        root /var/www;

        location / {
            root /var/www;
        }
    }
}
```

There are two kinds of directives:

- **Simple directives** — a name, some parameters, and a terminating `;`
  (e.g. `root /var/www;`).
- **Block directives** — a name, optional parameters, and a `{ ... }` body
  (e.g. `server { ... }`).

Comments start with `#` and run to the end of the line.

---

## Value syntax

Parameters are lexed into typed tokens. Several directives accept size or time
values with a unit suffix:

| Suffix | Meaning       | Example | Resolves to        |
|--------|---------------|---------|--------------------|
| `k`    | kilobytes     | `16k`   | `16000` bytes      |
| `m`    | megabytes     | `1m`    | `1000000` bytes    |
| `s`    | seconds       | `30s`   | `30` seconds       |
| `h`    | hours         | `1h`    | `3600` seconds     |
| (none) | raw number    | `1024`  | `1024`             |

Notes:

- Size suffixes use decimal (1000), **not** binary (1024) multipliers.
- A bare number is accepted anywhere a size or time is expected; it is taken
  as bytes or seconds respectively.
- Directives that expect a size accept `number` or `memory` tokens; directives
  that expect a duration accept `number` or `time` tokens.

---

## Inheritance

Settings cascade downward: `http` → `server` → `location` → nested
`location`. When a block is created it first copies the resolved configuration
of its parent, then applies its own directives on top. A directive set in a
`location` overrides the value inherited from its `server`, which in turn
overrides the value from `http`.

Not every directive is inherited — `listen`, `server_name`, `return`,
`cgi_pass`, `cgi_param`, and `limit_except` are specific to the block they are
declared in (see the [availability matrix](#directive-availability-matrix)).

---

## Context blocks

### `http`

- **Context:** top level (exactly one per file)
- **Body:** `server` blocks, a `types` block, and any inheritable simple
  directive that sets defaults for every server below it.

If no `server` block is present, a single default server is synthesised from
the `http`-level settings.

### `server`

- **Context:** inside `http`
- **Parameters:** none
- **Body:** `location` blocks, a `types` block, and server-level directives.

A server is selected for a request by matching the request's `Host` header
against its [`server_name`](#server_name) on the address:port it
[`listen`](#listen)s on. If no name matches, the `default_server` for that
address:port is used. If a server defines no `location /`, one is created
automatically so there is always a fallback location.

### `location`

- **Context:** inside `server` or another `location`
- **Parameters:** a matching path (see below)
- **Body:** location-level directives and further nested `location` blocks.

Location matching:

- **Prefix match (default):** `location /images { ... }` matches any URI that
  begins with `/images`. When several prefixes match, the **longest** one wins.
- **Exact / suffix match:** prefix a `\` to the path to switch off prefix
  matching, e.g. `location \ /exact { ... }`.

Two locations in the same block may not share the same path.

### `types`

- **Context:** inside `http`, `server`, or `location`
- **Parameters:** none
- **Body:** MIME type → extension mappings (see [`types`](#types-1) in the
  reference).

Declaring a `types` block **replaces** the inherited type map entirely rather
than merging with it.

---

## Directive reference

### Connection & networking

#### `listen`

```
listen <address>[:<port>] [default_server] [backlog=<n>];
```

- **Context:** `server`
- **Default:** `127.0.0.1:80`, backlog `5`, not default
- **May repeat:** yes (a server can listen on several address:port pairs)

Sets an address and port the server binds to.

- `<address>` — an IPv4 dotted-quad. A bare port (`listen 8080;`) keeps the
  default address `127.0.0.1`.
- `default_server` — marks this server as the fallback for its address:port
  pair when no `server_name` matches the request. Only one default server is
  allowed per address:port pair.
- `backlog=<n>` — the `listen(2)` backlog for the socket. All servers sharing
  an address:port pair must agree on the backlog value.

```nginx
listen 127.0.0.1:8080 default_server;
listen 8080;
listen 0.0.0.0:80 backlog=128;
```

#### `server_name`

```
server_name <name> [<name> ...];
```

- **Context:** `server`
- **Default:** none (empty)
- **May repeat:** yes; names accumulate

Names this server responds to. The request's `Host` header is matched against
these names to pick a server among those sharing an address:port pair. Two
servers on the same address:port pair may not register the same name.

---

### Request limits & timeouts

#### `client_header_buffer_size`

```
client_header_buffer_size <size>;
```

- **Context:** `http`, `server`
- **Default:** `1024` (bytes)

Size of the buffer used to read a client's request header. Requests with
headers larger than this fall back to the large-buffer pool.

#### `large_client_header_buffers`

```
large_client_header_buffers <count> <size>;
```

- **Context:** `http`, `server`
- **Default:** `4` buffers of `8192` bytes

Number and size of the large buffers used for oversized request headers.

#### `client_header_timeout`

```
client_header_timeout <duration>;
```

- **Context:** `http`, `server`
- **Default:** `60` (seconds)

Maximum time allowed to receive the full request header. On expiry the request
is terminated with `408 Request Time-out`.

#### `client_body_buffer_size`

```
client_body_buffer_size <size>;
```

- **Context:** `http`, `server`, `location`
- **Default:** `8192` (bytes)

Size of the buffer used to read a client's request body.

#### `client_max_body_size`

```
client_max_body_size <size>;
```

- **Context:** `http`, `server`, `location`
- **Default:** `1048576` (1 MiB, expressed as bytes)

Maximum allowed request body size. A body exceeding this limit is rejected
with `413 Request Entity Too Large`.

```nginx
client_max_body_size 100;   # 100 bytes
client_max_body_size 1k;    # 1000 bytes
client_max_body_size 5m;    # 5000000 bytes
```

#### `client_body_timeout`

```
client_body_timeout <duration>;
```

- **Context:** `http`, `server`, `location`
- **Default:** `60` (seconds)

Timeout applied **between** successive reads of the request body, not for the
body as a whole.

---

### Content & routing

#### `root`

```
root <path>;
```

- **Context:** `http`, `server`, `location`
- **Default:** `html`

Filesystem directory that the request URI is resolved against. The parameter
must be a path token.

#### `index`

```
index <filename>;
```

- **Context:** `http`, `server`, `location`
- **Default:** `index.html`

File served when a directory is requested. Mutually exclusive with
[`autoindex`](#autoindex) in the same block — setting one disables the other.

#### `autoindex`

```
autoindex <true|false>;
```

- **Context:** `http`, `server`, `location`
- **Default:** `false`

When `true`, a directory request with no matching index file produces a
generated directory listing. Only `true` or `false` are accepted. Setting
`autoindex` clears any [`index`](#index); the two cannot both be set in one
block.

#### `limit_except`

```
limit_except <method> [<method> ...];
```

- **Context:** `location`
- **Default:** `GET`, `POST`, `DELETE` all allowed

Restricts the HTTP methods permitted for this location to those listed.
Recognised methods are `GET`, `POST`, and `DELETE`. A request using a method
not listed receives `405 Method Not Allowed`.

```nginx
location /readonly {
    limit_except GET;      # only GET allowed here
}
```

#### `return`

```
return [<status>] <path-or-url>;
```

- **Context:** `location`
- **Default:** none (no redirect)

Issues a redirect for this location.

- `<status>` — optional 3-digit status code (must be in `100`–`599`). If
  omitted the server chooses an appropriate default.
- `<path-or-url>` — an internal path (e.g. `/index.html`) for an internal
  redirect, or an absolute URL (e.g. `https://example.com/`) for an external
  redirect.

```nginx
location /old {
    return 301 /index.html;          # internal redirect
}
location /ext {
    return 302 https://example.com/; # external redirect
}
```

---

### CGI

#### `cgi_pass`

```
cgi_pass <interpreter-path>;
```

- **Context:** `location`
- **Default:** unset

Enables CGI for this location and names the interpreter/executable used to run
scripts (e.g. `/usr/bin/python3`).

```nginx
location /cgi-bin {
    root /var/www;
    cgi_pass /usr/bin/python3;
}
```

#### `cgi_param`

```
cgi_param <name> <value>;
```

- **Context:** `location`
- **Default:** none
- **May repeat:** yes; each occurrence adds one variable

Adds an environment variable passed to the CGI process. Takes exactly a name
and a value.

```nginx
location /cgi-bin {
    cgi_pass /usr/bin/python3;
    cgi_param APP_ENV testing;
    cgi_param SECRET_KEY hunter2;
}
```

---

### MIME types

#### `types`

```
types {
    <mime/type>  <ext> [<ext> ...];
    ...
}
```

- **Context:** `http`, `server`, `location`
- **Default map:** `html → text/html`, `gif → image/gif`,
  `jpeg → image/jpeg`

Block directive mapping file extensions to MIME types. Each line inside the
block names a MIME type followed by one or more extensions that resolve to it.
A `types` block replaces the inherited map completely; nested blocks inside
`types` are not allowed.

```nginx
types {
    text/html   html htm;
    image/png   png;
    image/jpeg  jpeg jpg;
}
```

#### `default_type`

```
default_type <mime/type>;
```

- **Context:** `http`, `server`, `location`
- **Default:** `text/plain`

MIME type used for files whose extension is not present in the type map.

---

### Error handling

#### `error_page`

```
error_page <code> [<code> ...] [= | =<code>] <path-or-url>;
```

- **Context:** `http`, `server`, `location`
- **Default:** built-in `404 → error_page.html`
- **May repeat:** yes

Maps one or more HTTP status codes to an error document.

- One or more numeric `<code>` values precede the page.
- An optional response-code override changes the status actually returned to
  the client:
  - `=` on its own — use the status code produced by the internal redirection.
  - `=<code>` — force the response to the given status code. For URL targets
    the override must be a `3xx` code.
- `<path-or-url>` — an internal path, or an absolute URL. When the target is a
  URL the redirect is external and defaults to `302` if no override is given.

Within a single block, the first `error_page` directive clears the inherited
error map before adding its entries.

```nginx
error_page 404 /404.html;
error_page 500 501 502 503 504 /50x.html;
error_page 404 =200 /empty.gif;              # serve page but respond 200
error_page 403 = /fallback;                  # keep the redirection's status
```

---

## Directive availability matrix

| Directive                     | `http` | `server` | `location` |
|-------------------------------|:------:|:--------:|:----------:|
| `root`                        |   ✓    |    ✓     |     ✓      |
| `listen`                      |        |    ✓     |            |
| `server_name`                 |        |    ✓     |            |
| `client_header_buffer_size`   |   ✓    |    ✓     |            |
| `large_client_header_buffers` |   ✓    |    ✓     |            |
| `client_header_timeout`       |   ✓    |    ✓     |            |
| `client_body_buffer_size`     |   ✓    |    ✓     |     ✓      |
| `client_max_body_size`        |   ✓    |    ✓     |     ✓      |
| `client_body_timeout`         |   ✓    |    ✓     |     ✓      |
| `index`                       |   ✓    |    ✓     |     ✓      |
| `autoindex`                   |   ✓    |    ✓     |     ✓      |
| `default_type`                |   ✓    |    ✓     |     ✓      |
| `types` (block)               |   ✓    |    ✓     |     ✓      |
| `error_page`                  |   ✓    |    ✓     |     ✓      |
| `limit_except`                |        |          |     ✓      |
| `return`                      |        |          |     ✓      |
| `cgi_pass`                    |        |          |     ✓      |
| `cgi_param`                   |        |          |     ✓      |
| `server` (block)              |   ✓    |          |            |
| `location` (block)            |        |    ✓     |     ✓      |

Most single-value directives may appear only once per block; declaring one
twice in the same block is a configuration error. Directives that accumulate
values (`listen`, `server_name`, `cgi_param`, `error_page`) may repeat.
