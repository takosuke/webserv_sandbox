#pragma once

#include <sstream>

#include <stdint.h>

class ClientConnection;

/* CLIENT CALLBACK ************************************************************/

class ClientCallback {
public:
	ClientCallback()
		: _client(NULL), _events(0) { };
	ClientCallback(ClientConnection * client, uint32_t events)
		: _client(client), _events(events) { };
	ClientCallback(const ClientCallback & other);
	~ClientCallback() { };

	ClientCallback & operator=(const ClientCallback & other);

	void	set_client(ClientConnection * client) { _client = client; };
	void	set_events(uint32_t events) { _events = events; };

	void	callback();

private:
	ClientConnection *	_client;
	uint32_t			_events;
};

#define BYTES_PER_READ_CYCLE 1024
#define BYTES_PER_WRITE_CYCLE 1024

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

	FileConnection(const ClientCallback & callback)
		: state(0), _callback(callback), _fd(-1),
		_tmp_buffer(NULL), _buffer() { };
	virtual ~FileConnection();

	int		set_operation_size(size_t buffer_size);
	void	set_callback(ClientCallback & other) { _callback = other; };
	void	set_fd(int fd) { _fd = fd; };

	const std::string &	get_buffer() const { return (_buffer); };

	virtual int		open_file(const std::string & file) = 0;

	virtual void	handle() = 0;

protected:
	FileConnection();

	ClientCallback		_callback;

	int					_fd;

	char *			_tmp_buffer;
	size_t			_tmp_buffer_size;
	std::string		_buffer;
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
	ReadFileConnection(const ClientCallback & callback) 
		: FileConnection(callback) { };
	~ReadFileConnection() { };

	int		open_file(const std::string & file);

	void	handle();

private:
	ReadFileConnection();
};

class WriteFileConnection : public FileConnection {
public:
	WriteFileConnection(const ClientCallback & callback) 
		: FileConnection(callback) { };
	~WriteFileConnection() { };

	int		open_file(const std::string & file);

	void	handle();

private:
	WriteFileConnection();
};
