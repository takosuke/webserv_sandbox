#include "EntityConnection.hpp"

// BYTES_PER_READ_CYCLE macro
#include "Response.hpp"

#include <unistd.h>

EntityConnection::~EntityConnection() {
	delete _buffer;
}

void EntityConnection::set_fd(int fildes) {
	fd = fildes;
}

void	EntityConnection::set_callback(Connection * callback, uint32_t events, std::string & buffer) {
	_callback = callback;
	_callback_events = events;
	_callback_stream.str(buffer);
}

int EntityConnection::set_buffer_size(size_t buffer_size) {
	delete _buffer;
	_buffer_size = buffer_size;
	_buffer = new char[buffer_size];
	if (_buffer == NULL)
		state = -1;
	return (state);
}

/* @brief Sets the events for this connection in `loop` to 0 and sets up the
 * callback events for the callback Connection provided in the constructor.
 */
void EntityConnection::handle_callback(EpollLoop & loop) {
	loop.mod(_callback, _callback_events);
	loop.mod(this, 0);
}

/* READ ENTITY CONNECTION *****************************************************/

void ReadEntityConnection::handle(EpollLoop & loop, uint32_t events) {
	events = 0;
	/* safety measure to ensure we always have a buffer */
	if (_buffer == NULL && set_buffer_size(BYTES_PER_READ_CYCLE) < 0)
		return ;

	int		bytes = read(fd, _buffer, _buffer_size);
	if (bytes < 0) {
		state = -1;
	} else {
		_callback_stream.write(_buffer, bytes);
		if (_callback_stream.bad() || _callback_stream.fail())
			state = -1;
	}
	if (bytes <= 0 || state < 0) {
		handle_callback(loop);
	}
}

/* WRITE ENTITY CONNECTION ****************************************************/

void WriteEntityConnection::handle(EpollLoop & loop, uint32_t events) {
	events = 0;
	/* safety measure to ensure we always have a buffer */
	if (_buffer == NULL && set_buffer_size(BYTES_PER_WRITE_CYCLE) < 0)
		return ;

	int	save_pos = _callback_stream.tellg();
	_callback_stream.read(_buffer, _buffer_size);
	if (_callback_stream.bad() || _callback_stream.fail())
		state = -1;
	else {
		int bytes = write(fd, _buffer, _buffer_size);
		if (bytes < 0)
			state = -1;
		else
			_callback_stream.seekg(save_pos + bytes, std::ios::beg);
	}
	if (state < 0 || _callback_stream.eof()) {
		handle_callback(loop);
	}
}
