# Configuration

This document lists the different directives a `webserv` configuration file allows, as well as their parameters.

## Token types

For all of these tokens spaces and ';' (semicolon) are treated as delimiters.
Escaping, single and double quotationmarks are not supported.

### string

Any string

### path

A string starting with '/'

### url

A string starting with "\<scheme\>:"

### number

A number as recognized by `std::strtoul()`. Doesn't accept signed numbers as all values the configuration files are dealing with should be positive.

### memory

A number ending in either 'k' (KiB) or 'm' (MiB)

### time

A number ending in either 's' (seconds) or 'h' (hours)

## Directives

## Main Body Directives

These directives mostly exist to seperate different blocks of the configuration from each other. They inherit certain settings from each other. `location` inherits from `server` inherits from `http`. If a default server was declared for a address:port pair it's settings are used as default for other servers on this address:port pair unless they have specific settings for the directives themselves.

## `http`
```
Syntax:     http { ... }
Default:    ---
Context:    main
```

Provides the configuration file context in which the HTTP server directives are specified.

## `server`
```
Syntax:     server { ... }
Default:    ---
Context:    http
```

Sets configuration for a virtual server.

## `location`
```
Syntax:     location [\] uri { ... }
Default:    ---
Context:    server, location
```

Sets configuration for a location on a server. The location directives takes a uri parameter that determines where on the server it is reachable. F.e. A location with the uri `www` would be rechable on `http://address:port/www`.

If you nest locations inside each other they will only be reachable under their parent location. F.e. a location with the uri `html` inside the location of the previous example would be reachable on `http://address:port/www/html`.

Normally locations are resolved by looking for the longest match in the request uri against the available locations uris. This process is repeated with an updated set of locations until there is no better match than the current location.

Before the uri an optional '\\' parameter can be provided to change the way the location is looked up. This marks the location as being an extension matching on. The location resolver will look through a list of the extension matching locations during request resolution and pick the longest matching one. If none match it will result to the regular location resolution process described above.

This also means that extension matching locations will always have priority over regular locations. If you need to seperate them to enable lookup of certain already mapped extensions you can do so by using nested locations:

```
root /var/www/html/scripts;

location / {
    # This location allows a non interpreted view of the scripts
    # If the '\ .py' location was in the same scope as this one
    # The scripts would only be available as dynamic content from the CGI
}

location /cgi {
    # This location goes to it's nested location for all python scripts
    # and returns the CGI rendered content instead of the script
    location \ .py {
        cgi_pass ...;
    }
}
```

## Server specific directives

## `server_name`
```
Syntax:     server_name name ...;
Default:    server_name "";
Context:    server
```

Provides one or more virtual names for the server it is in. The server name is used in server resolution after recieving a requests headers based on the "Host" header field. This way multiple server configurations are available on the same address:port.

## `listen`
```
Syntax:     listen address[:port] [default_server] [backlog=number];
Default:    listen 127.0.0.1:80 5;
Context:    server
```

Configures the address:port pair that a server configuration is reachable from.

If no port was provided as an argument the port 80 is assumed.

The optional `default_server` set's this server to be the default one during server resolution if no matching virtual server name was found. Only one `listen` directive for the same address:port pair can contain a `default_server` parameter. If no listen directive for an address:port pair conains this parameter the first server encoutnered in the configuartion file that contains this specific pair will be the default server.

The optional `backlog` parameter configures the backlog parameter given to the listen() function. If multiple listen directives for the same address:port pair have different values for this parameter the default server's value will prevail as true.

## Location specific directives

## `limit_except`
```
Syntax:     limit_except method ...;
Default:    limit_except GET POST DELETE;
Context:    location
```

Configures the HTTP methods allowed to be used for this location.

Any request using a method not listed in limit except used for the location will be declined with a "405 Method Not Allowed" response.

## `root`
```
Syntax:     root path;
Default:    root html;
Context:    http, server, location
```

Configures the root directory used as default (if in `http`), for a specific server and all it's locations (if in `server`), or for a specific location (if in `location`).

Path resolution is the same as in regular linux use ('/' prepended for absolute, otherwise relative).

### CGI

## `cgi_pass`
```
Syntax:     cgi_pass uri;
Default:    ---
Context:    location
```

Configures a CGI to be used for this location. The uri is either a url or a path to the CGI that should handle requests to this location.

## `cgi_param`
```
Syntax:     cgi_param variable value;
Default:    ---
Context:    location
```

Configures additional parameters given to the CGI handler. The `value` will be given with the `variable` as a variable name. Multiple of these directives can be applied to a single cgi location.

## Redirections

## `error_page`
```
Syntax:     error_page code ... [=response] uri;
Default:    error_page 404 404.html;
Context:    http, server, location
```

Configures a uri redirect for a set of error codes.

If the optional `=response` parameter is given the response code will be overwritten by the response code provided after the `=` (equal sign).

Every number before the optional `=response` or uri parameter will be added to the set of codes that should result in a redirection to the specified uri.

## `return`
```
Syntax:     redirect [response] uri;
Default:    ---
Context:    location
```

Configures a redirection to a uri.

If the optinal `response` parameter is given the default response code "302 Moved Permanently" is overwritten with the given response code.

## Client Header

## `client_header_buffer_size`
```
Syntax:     client_header_buffer_size memory;
Default:    client_header_buffer_size 1k;
Context:    http, server
```

Configures the size of the buffer used to read the clients headers.

## `client_header_timeout`
```
Syntax:     client_header_timeout time;
Default:    client_header_timeout 60s;    
Context:    http, server
```

Configures the timeout duration while reading the clients headers.

## Client Body

## `client_body_buffer_size`
```
Syntax:     client_body_buffer_size time;
Default:    client_body_buffer_size 8k;
Context:    http, server, location
```

Configures the size of the buffer used to read the clients body.

## `client_body_timeout`
```
Syntax:     client_body_timeout time;
Default:    client_body_timeout 60s;
Context:    http, server, location
```

Configures the timeout duration while reading the clients body.

## `client_max_body_size`
```
Syntax:     client_max_body_size memory;
Default:    client_max_body_size 1m;
Context:    http, server, location
```

Configures the maximum size a clients headers are supposed to have.

## Types

## `types`
```
Syntax:     types { ... }
Default:    types {
                text/html   html;
                image/gif   gif;
                image/jpeg  jpeg;
            }
Context:    http, server, location
```

Configures the extension to mime-type matching that should be used for the "Content-Type" response header field.

Each directive inside the types body-directive consists of a leading mime-type and a set of extensions of files that are mapped to it.

## `default_type`
```
Syntax:     default_type type;
Default:    default_type text/plain;
Context:    http, server, location
```

Configures the default mime-type to be returned if no match was found during the extension lookup.

## Indexing

## `index`
```
Syntax:     index file ...;
Default:    index index.html;
Context:    http, server, location
```

Configures the file to be used as an index if the request is for a directory.

## `autoindex`
```
Syntax:     autoindex true | false;
Default:    autoindex false;
Context:    http, server, location
```

Configures whether autoindexing should be turned on or off.

## Miscellaneous

## `output_buffer`
```
Syntax:     output_buffer memory;
Default:    output_buffer 32k;
Context:    http, server, location
```

Configures the size of the buffer used to read files from the disc to the client.

## `upload_directory`
```
Syntax:     upload_directory path location [create_path];
Default:    upload_directory upload upload;
Context:    http, server, location
```

Configures the directory and returned location for uploads via POST.

Uploaded files will be stored in `path` with all of the non location specifiers of a uri appended to it.

The response contains a "Location" header field with `location` with all of the non location specifiers of a uri appended to it. The location should be where on your servers location mapping you want uploaded to be available.

If the optional `create_path` parameter is provided nested directories are enabled and will be created during the file upload.
