# Connection Lifecycle

The Lifecycle of a Connection is as follows:

- A client makes a connection to a listening server socket on it's specified port
- The server creates a new connection socket for the client and adds that connection to epoll
- The client socket is read from to get the headers
- The virtual server configuration and location configuration is determined based on the provided headers
- The request uri is checked against the locations configuration and redirected if necessary


- Client connects to server socket
- Server creates a new client socket and adds it to epoll
- Client socket is read from to get headers
- Client headers are parsed and saved
- Client body (if existing) is read and saved to a temporary file
- Server and location configurations are determined based on the headers

