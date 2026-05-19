#include "FileConnection.hpp"

#include "EpollLoop.hpp"

// BYTES_PER_READ_CYCLE macro
#include "Response.hpp"

#include <unistd.h>

FileConnection::~FileConnection() {
	delete [] (_buffer);
}

void FileConnection::set_fd(int fildes) {
	_fd = fildes;
}

void	FileConnection::set_callback(ClientConnection * callback, uint32_t events, std::string & buffer) {
	_callback = callback;
	_callback_events = events;
	_callback_buffer = buffer;
	_stream.str(buffer);
}

int FileConnection::set_buffer_size(size_t buffer_size) {
	delete [] (_buffer);
	_buffer_size = buffer_size;
	_buffer = new char[buffer_size];
	if (_buffer == NULL)
		state = -1;
	return (state);
}

/* @brief Sets the events for this connection in `loop` to 0 and sets up the
 * callback events for the callback Connection provided in the constructor.
 */
void FileConnection::handle_callback() {
	_callback_buffer = _stream.str();
	_callback->construct_response(200);
	EpollLoop::get_instance().mod(_callback, _callback_events);
	FileLoop::get_instance().del(this);
}

/* READ ENTITY CONNECTION *****************************************************/

void ReadFileConnection::handle() {
	/* safety measure to ensure we always have a buffer */
	if (_buffer == NULL && set_buffer_size(BYTES_PER_READ_CYCLE) < 0)
		return ;

	int		bytes = read(_fd, _buffer, _buffer_size);
	if (bytes < 0) {
		state = -1;
	} else {
		_stream.write(_buffer, bytes);
		if (_stream.bad() || _stream.fail())
			state = -1;
	}
	if (bytes <= 0 || state < 0) {
		handle_callback();
	}
}

/* WRITE ENTITY CONNECTION ****************************************************/

void WriteFileConnection::handle() {
	/* safety measure to ensure we always have a buffer */
	if (_buffer == NULL && set_buffer_size(BYTES_PER_WRITE_CYCLE) < 0)
		return ;

	int	save_pos = _stream.tellg();
	_stream.read(_buffer, _buffer_size);
	if (_stream.bad() || _stream.fail())
		state = -1;
	else {
		int bytes = write(_fd, _buffer, _buffer_size);
		if (bytes < 0)
			state = -1;
		else
			_stream.seekg(save_pos + bytes, std::ios::beg);
	}
	if (state < 0 || _stream.eof()) {
		handle_callback();
	}
}
