#include "FileBodyCache.hpp"

FileBodyCache::FileBodyCache() {

}

FileBodyCache &FileBodyCache::get_instance() {
	static	FileBodyCache	instance;
	return (instance);
}

int FileBodyCache::add(const std::string &filename) {
	std::ifstream	file = std::ifstream(filename);
	char			buffer[1024];
	std::string		body;

	if (_bodies.contains(filename))
		return (1);
	if (!file.is_open())
		return (0);
	while (!file.fail() && !file.eof()) {
		file.get(buffer, 1024, '\0');
		body.append(buffer);
	}
	if (file.fail())
		return (0);
	_bodies.insert(std::make_pair(filename, body));
	return (1);
}

void FileBodyCache::del(const std::string &filename) {
	std::map<std::string, std::string>::iterator it = _bodies.find(filename);
	if (it != _bodies.end())
		_bodes.erase(it);
}

const std::string &FileBodyCache::get(const std::string &filename) const {
	std::map<std::string, std::string>::iterator it = _bodies.find(filename);
	if (it != _bodies.end())
		return (&(it->second));
	return (NULL);
}
