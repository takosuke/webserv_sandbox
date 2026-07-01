#include "Connection.hpp"

#include "EpollLoop.hpp"

class ClientConnection: public Connection {
private:
	ClientConnection() { };

	enum {
		REQ_HEADERS,
		REQ_BODY,
		CGI_IN_BODY,
		CGI_OUT_HEADERS,
		CGI_OUT_BODY,
		RES_HEADERS,
		RES_BODY
	}	_state;

	int	_client_fd;
	int	_tmpfile_fd;
	int	_cgiin_fd;
	int	_cgiout_fd;
	int	_staticfile_fd;

public:
	ClientConnection(int fildes, Http *config);

	void	handle(uint32_t	events);
};

void ClientConnection::handle(uint32_t events) {
	if (events & EPOLLERR) {
		EpollLoop::get_instance().del(this);
	} else if (events & EPOLLIN) {
		// Fill buffer. If the buffer is full and we can't write it means that
		// the headers are too large.
	}
}
