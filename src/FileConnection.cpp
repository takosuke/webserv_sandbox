#include "FileConnection.hpp"

#include "EpollLoop.hpp"

// BYTES_PER_READ_CYCLE macro
#include "Response.hpp"

#include <unistd.h>
#include <fcntl.h>

/* CLIENT CALLBACK ************************************************************/

ClientCallback::ClientCallback(const ClientCallback & other) {
	*this = other;
}

ClientCallback & ClientCallback::operator=(const ClientCallback & other) {
	if (this == & other)
		return (*this);
	_client = other._client;
	_events = other._events;
	return (*this);
}

void ClientCallback::callback() {
	_client->construct_response(200);
	EpollLoop::get_instance().mod(_client, _events);
}

/* FILE CONNECTION ************************************************************/

FileConnection::~FileConnection() {
	delete [] (_tmp_buffer);
}

int FileConnection::set_operation_size(size_t buffer_size) {
	delete [] (_tmp_buffer);
	_tmp_buffer_size = buffer_size;
	_tmp_buffer = new char[_tmp_buffer_size];
	if (_tmp_buffer == NULL)
		state = 500;
	return (state);
}

/* READ FILE CONNECTION *******************************************************/

int ReadFileConnection::open_file(const std::string & file) {
	_fd = open(file.c_str(), O_RDONLY);
	if (_fd < 0)
		state = 500;
	return (_fd < 0);
}

void ReadFileConnection::handle() {
	/* safety measure to ensure we always have a buffer */
	if (_tmp_buffer == NULL && set_operation_size(BYTES_PER_READ_CYCLE) < 0)
		return ;

	int		bytes = read(_fd, _tmp_buffer, _tmp_buffer_size);
	if (bytes < 0) {
		state = 500;
	} else {
		try {
			_buffer.append(_tmp_buffer, bytes);
		} catch (std::exception & e) {
			state = 500;
		}
	}
	if (state != 0 || bytes == 0) {
		_callback.callback();
		FileLoop::get_instance().del(this);
	}
}

/* WRITE FILE CONNECTION ******************************************************/

int WriteFileConnection::open_file(const std::string & file) {
	_fd = open(file.c_str(), O_WRONLY);
	if (_fd < 0)
		state = 500;
	return (_fd < 0);
}

void WriteFileConnection::handle() {
	/* safety measure to ensure we always have a buffer */
	if (_tmp_buffer == NULL && set_operation_size(BYTES_PER_WRITE_CYCLE) < 0)
		return ;

	// int	save_pos = _buffer.tellg();
	// _buffer.read(_tmp_buffer, _tmp_buffer_size);
	// if (_buffer.bad() || _buffer.fail())
	// 	state = 500;
	// else {
	// 	int bytes = write(_fd, _tmp_buffer, _tmp_buffer_size);
	// 	if (bytes < 0)
	// 		state = 500;
	// 	else
	// 		_buffer.seekg(save_pos + bytes, std::ios::beg);
	// }
	if (state != 0) {
		_callback.callback();
		FileLoop::get_instance().del(this);
	}
}
