#include "autoindex.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <ctime>
#include <cerrno>

#include <string>
#include <sstream>

autoindex::File::File() { };

/*	Expects dirpath to be a path to a directory terminated by a '/'.
 */
autoindex::File::File(const std::string & dirpath, struct dirent & dirent) {
	struct stat	statbuf;

	_name = dirent.d_name;
	std::string	filepath = std::string(dirpath + _name);
	_err = 0;
	if (stat(filepath.c_str(), &statbuf) < 0 && errno != 0) {
		_err = errno;
	} else {
		_type = statbuf.st_mode & S_IFMT;
		_size = 0;
		if (S_IFREG & _type || S_IFLNK & _type) {
			_size = statbuf.st_size;
		}
		gmtime_r(&statbuf.st_mtim.tv_sec, &_mtime);
	}
	_hidden = false;
	if (_name.size() > 1 && _name[0] == '.' && _name[1] != '.')
		_hidden = true;
}

autoindex::File::~File() { }

std::string autoindex::File::name() const {
	return (_name);
}

std::tm autoindex::File::mtime() const {
	return (_mtime);
}

mode_t autoindex::File::type() const {
	return (_type);
}

off_t autoindex::File::size() const {
	return (_size);
}

bool autoindex::File::is_socket() const {
	return (_type == S_IFSOCK);
}

bool autoindex::File::is_symlink() const {
	return (_type == S_IFLNK);
}

bool autoindex::File::is_regular() const {
	return (_type == S_IFREG);
}

bool autoindex::File::is_block_device() const {
	return (_type == S_IFBLK);
}

bool autoindex::File::is_directory() const {
	return (_type == S_IFDIR);
}

bool autoindex::File::is_character_device() const {
	return (_type == S_IFCHR);
}

bool autoindex::File::is_pipe() const {
	return (_type == S_IFIFO);
}

bool autoindex::File::is_self() const {
	if (_name.size() == 1 && _name[0] == '.')
		return (true);
	return (false);
}

bool autoindex::File::hidden() const {
	return (_hidden);
}

bool autoindex::File::fail() const {
	return (_err == 0);
}

int autoindex::File::err() const {
	return (_err);
}
