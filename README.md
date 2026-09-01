*This project has been created as part of the 42 curriculum by abronner, marcampo.*

# Description

**webserv** is about creating a basic program that allows serving of static and dynamic content to clients from anywhere on the web.

For this we needed to look into the linux socket api:
- [`socket`](https://man7.org/linux/man-pages/man2/socket.2.html)
- [`bind`](https://man7.org/linux/man-pages/man2/bind.2.html)
- [`listen`](https://man7.org/linux/man-pages/man2/listen.2.html)
- [`accept`](https://man7.org/linux/man-pages/man2/accept.2.html)
- [`connect`](https://man7.org/linux/man-pages/man2/connect.2.html)

as well as how to implement non-blocking I/O operations with one of three systems designed for checking available operations on file descriptors (f.e. sockets):

- [`select`](https://man7.org/linux/man-pages/man2/select.2.html)
- [`poll`](https://man7.org/linux/man-pages/man2/poll.2.html)
- [`epoll`](https://man7.org/linux/man-pages/man7/epoll.7.html)

Theses are the main building blocks to implement communication with other devices on the internet. The biggest task for this project though was to implement a HTTP protocol. For simplicity's sake we chose HTTP/1.0 since it implements a limited set of features with the possibility to add onto them what we found useful and/or was required by the subject for this project.

Our **webserv** implements three methods:

- `GET` (required by the subject and HTTP/1.0)
- `POST` (required by the subject and HTTP/1.0)
- `DELETE` (required by the subject)

# Instructions

The compilation of the program is managed via a `Makefile` provided with the project. The `Makefile` provides these rules:

- `all` compiles the program
- `webserv` compiles the program
- `clean` cleans up the object and dependency files
- `fclean` cleans up the object, dependency and program files
- `re` cleans up the object, dependency and program files and recompiles the program
- `install-clojure` installs clojure (needed for testing the server)
- `prepare-confs` prepare template configurations provided with the program
- `test` conducts tests with clojure on the prepared configuration files

## Starting the server

The compiled program has one valid way to be called. The program is to be provied with one file or path to a file that refers to a configuration file, ending in `.conf`.

The program will read the configuration file, check for any syntax errors in the configuration and abort if one was found.

If the configuration is clean, the program will start a server with a port listener for each provided different `listen` directive.

For more information on the directives refer to the `CONFIGURATION.md`.

# Resources

Resources used were:

- Manpages for the different required and allowed fucntions 
- [RFC 1945](https://datatracker.ietf.org/doc/html/rfc1945) for information about HTTP/1.0
- [RFC 3875](https://datatracker.ietf.org/doc/html/rfc3875) for information about CGI
- [Mozilla Developer Network](https://developer.mozilla.org/en-US/docs/Web/HTTP) for additional information and examples of methods and HTTP functionality

Additionally a bunch of small tutorials and specific blog/forum posts were used for specific issues.

AI was used as a helper for advanced rubber ducking, test creation and code review. I (Mario) thought to use Clojure for test writing as it's a language I enjoy greatly, but trying to get 2 real world projects started in 2 unfamiliar languages turned out to be too much of a mental burden. Having lost one team member at the beginning of the project, I decided to enlist the help of AI to write tests and help organize the bug hunt, but all the core logic and architecture was created by us. 
