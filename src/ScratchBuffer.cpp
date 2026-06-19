#include "ScratchBuffer.hpp"

#include <cstring>

ScratchBuffer::ScratchBuffer() : _ref_data(false), capacity(1024), readpos(0), writepos(0) {
	data = new char[capacity];
}

ScratchBuffer::ScratchBuffer(size_t cap) : _ref_data(false), capacity(cap), readpos(0), writepos(0) {
	data = new char[capacity];
}

ScratchBuffer::ScratchBuffer(const ScratchBuffer &other) {
	*this = other;
}

ScratchBuffer::~ScratchBuffer() {
	if (!_ref_data)
		delete[] (data);
}

ScratchBuffer &ScratchBuffer::operator=(const ScratchBuffer &other) {
	if (this == &other)
		return (*this);
	set_capacity(other.capacity);
	readpos = other.readpos;
	writepos = other.writepos;
	std::memcpy(data, other.data, readpos);
	return (*this);
}

void ScratchBuffer::set_capacity(size_t cap) {
	char *new_data = new char[cap];

	if (new_data == NULL)
		throw (std::runtime_error("Couldn't create new data"));
	size_t	cpysize = std::min(sizeof(data), sizeof(new_data));
	std::memcpy(new_data, data, cpysize);
	if (!_ref_data)
		delete[] (data);
	_ref_data = false;
	data = new_data;
}

void ScratchBuffer::set_data(char *buf, size_t cap) {
	if (!_ref_data)
		delete[] (data);
	_ref_data = true;
	data = buf;
	capacity = cap;
	readpos = 0;
	writepos = 0;
}

size_t ScratchBuffer::fill(int fd) {
	int	readret = read(fd, data + readpos, fill_capacity());

	if (readret > 0)
		readpos += readret;
	data[readpos] = '\0';
	return (readret);
}

size_t ScratchBuffer::fill(std::fstream &fstream) {
	fstream.get(data + readpos, fill_capacity(), '\0');
	readpos += fstream.gcount();
	return (fstream.gcount());
}

size_t ScratchBuffer::fill(const char *str, size_t size) {
	if (size > fill_capacity())
		return (fill_capacity());
	std::strncpy(data + readpos, str, size);
	readpos += size;
	data[readpos] = '\0';
	return (size);
}

size_t ScratchBuffer::feed(int fd) {
	size_t	writeret = write(fd, data + writepos, readpos - writepos);

	if (writeret > 0)
		writepos += writeret;
	return (writeret);
}

size_t ScratchBuffer::feed(std::fstream &fstream) {
	fstream.write(data + writepos, readpos - writepos);
	writepos += fstream.gcount();
	return (fstream.gcount());
}

size_t ScratchBuffer::fill_capacity() {
	return (capacity - readpos - 1);
}

size_t ScratchBuffer::feed_capacity() {
	return (readpos - writepos);
}

#include <cstring>

char *strnchr(const char *ptr, char c, size_t count = -1) {
	size_t pos = 0;
	while (ptr[pos] != '\0' && pos < count) {
		if (ptr[pos] == c)
			return (const_cast<char *>(ptr + pos));
		++pos;
	}
	return (NULL);
}

size_t ScratchBuffer::find(char c, size_t searchpos) {
	char	*found = strnchr(data + searchpos, c, readpos - searchpos);

	if (found == NULL)
		return (npos);
	return (found - data);
}

char *strnstr(char *ptr, const std::string &str, size_t count = -1) {
	size_t pos = 0;
	
	while (ptr[pos] != '\0' && pos + str.size() <= count) {
		size_t	strpos = 0;

		while (ptr[pos + strpos] != '\0' && strpos < str.size()
			&& ptr[pos + strpos] == str[strpos]) {
			++strpos;
		}
		if (strpos == str.size())
			return (ptr + pos);
		++pos;
	}
	return (NULL);
}

size_t ScratchBuffer::find(const std::string &str, size_t searchpos) {
	char	*found = strnstr(data + searchpos, str, readpos - searchpos);

	if (found == NULL)
		return (npos);
	return (found - data);
}

void ScratchBuffer::clear() {
	readpos = 0;
	writepos = 0;
}

void ScratchBuffer::erase(size_t from, size_t to) {
	std::memmove(data + from, data + to, readpos - to);
	if (writepos > from)
		writepos -= to - from;
	readpos -= to - from;
	data[readpos] = 0;
}

bool ScratchBuffer::error() {
	return (data == NULL);
}
