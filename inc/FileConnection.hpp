#pragma once

#include <sstream>

#include <stdint.h>

#define BYTES_PER_READ_CYCLE 1024
#define BYTES_PER_WRITE_CYCLE 1024

class ClientConnection;

class FileConnection {
public:
	/* used to give the callee information if the operation was performed
	 * successfully.
	 * Set to 0 on success, -1 on any kind of failure. Since this connection
	 * is only responsible for handling reading or writing from and to a file
	 * the confirmation of existence of the file is left to the callee. Any
	 * non 0 state in this class should be interpreted as a server error.
	 */ 
	int		state;

	FileConnection(ClientConnection * callback, uint32_t events, std::string & buffer)
		: state(0), _buffer(NULL), _stream(buffer),
		_callback(callback), _callback_events(events), _callback_buffer(buffer) { };
	virtual ~FileConnection();

	void	set_fd(int fildes);
	int		set_buffer_size(size_t buffer_size);
	void	set_callback(ClientConnection * callback, uint32_t events, std::string & buffer);

	virtual void	handle() = 0;

protected:
	void			handle_callback();

	int				_fd;

	char *				_buffer;
	size_t				_buffer_size;
	std::stringstream	_stream;

	ClientConnection *	_callback;
	uint32_t			_callback_events;
	std::string &		_callback_buffer;
};

#include "ClientConnection.hpp"

/**	@biref Class that is meant to handle subsequent connections to files, CGI
 * 	or proxies. It needs to be provided a proper fd to the file or socket it is
 * 	supposed to handle.
 *
 * 	After construction the internal buffer needs to be set up by calling
 * 	`set_buffer_size`. The connection will always try to read `buffer_size`
 * 	bytes from the fd and store them in the buffer provided in the constructor
 * 	or by the menas of `set_callback`.
 *
 * 	Once a file has been fully read `handle_callback` modifies the Connection
 * 	itself so that this Connection will not be called again and modifies
 * 	the callback Connection provided with the events provided so that it can
 * 	operate again.
 *
 * 	Registration in the EpollLoop as well as deletion of the class needs to
 * 	happen in the calling Connection. Therefore a pointer to this class needs
 * 	to be saved there.
 *
 * 	Under optimal conditions a cllback and subconnection shouldn't be available
 * 	in the same EpollLoop return list, but to make sure the deletion of these
 * 	classes should happen from outside this loop.
 */
class ReadFileConnection : public FileConnection {
public:
	ReadFileConnection(ClientConnection * callback, uint32_t events, std::string & buffer) 
		: FileConnection(callback, events, buffer) { };
	~ReadFileConnection() { };

	void handle();
};

class WriteFileConnection : public FileConnection {
public:
	WriteFileConnection(ClientConnection * callback, uint32_t events, std::string & buffer)
		: FileConnection(callback, events, buffer) { };
	~WriteFileConnection() { };

	void	handle();
};
