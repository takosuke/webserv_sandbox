#pragma once

#include "Connection.hpp"
#include "EpollLoop.hpp"

#include <sstream>

#include <stdint.h>

#define BYTES_PER_READ_CYCLE 1024
#define BYTES_PER_WRITE_CYCLE 1024

class EntityConnection : public Connection {
public:
	/* used to give the callee information if the operation was performed
	 * successfully.
	 * Set to 0 on success, -1 on any kind of failure. Since this connection
	 * is only responsible for handling reading or writing from and to a file
	 * the confirmation of existence of the file is left to the callee. Any
	 * non 0 state in this class should be interpreted as a server error.
	 */ 
	int		state;

	EntityConnection() : state(0), _buffer(NULL), _callback(NULL), _callback_events(0) {};
	EntityConnection(Connection * callback, uint32_t events, std::string & buffer)
		: state(0), _callback(callback), _callback_events(events), _callback_stream(buffer) { };
	virtual ~EntityConnection();

	void	set_fd(int fildes);
	int		set_buffer_size(size_t buffer_size);
	void	set_callback(Connection * callback, uint32_t events, std::string & buffer);

protected:
	void			handle_callback(EpollLoop & loop);

	char *			_buffer;
	size_t			_buffer_size;

	Connection *		_callback;
	uint32_t			_callback_events;
	std::stringstream	_callback_stream;
};

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
class ReadEntityConnection : public EntityConnection {
public:
	ReadEntityConnection() {};
	ReadEntityConnection(Connection * callback, uint32_t events, std::string & buffer) 
		: EntityConnection(callback, events, buffer) {};
	~ReadEntityConnection();

	void handle(EpollLoop & loop, uint32_t events);

};

class WriteEntityConnection : public EntityConnection {
public:
	WriteEntityConnection() : EntityConnection() {};
	WriteEntityConnection(Connection * callback, uint32_t events, std::string & buffer)
		: EntityConnection(callback, events, buffer) {};
	~WriteEntityConnection() {};

	void	handle(EpollLoop & loop, uint32_t events);
};
