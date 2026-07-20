#pragma once

#include <fstream>

#include <stddef.h>
#include <unistd.h>

class ScratchBuffer {
private:
	bool	_ref_data;
public:
	static const size_t npos = -1;

	char	*data;
	size_t	capacity;
	size_t	readpos;
	size_t	writepos;

	ScratchBuffer();
	ScratchBuffer(size_t cap);
	ScratchBuffer(const ScratchBuffer &other); // Throws exception
	~ScratchBuffer();

	ScratchBuffer	&operator=(const ScratchBuffer &other);	// Throws exception

	void	set_capacity(size_t cap); // Throws exception
	void	set_data(char *buf, size_t cap);

	int		fill(int fd);
	int		fill(std::fstream &fstream);
	int		fill(const char *str, int size);

	int		feed(int fd);
	int		feed(std::fstream &fstream);

	int		fill_capacity();
	int		feed_capacity();

	bool	fill_eof();
	bool	feed_eof();

	size_t	find(char c, size_t searchpos = 0);
	size_t	find(const std::string &str, size_t searchpos = 0);

	void	clear();

	void	erase(size_t from = 0, size_t to = 0);

	bool	error();
};
