#include "ResponseCache.hpp"

ResponseCache & ResponseCache::get_instance() {
	static ResponseCache	instance;
	return instance;
}

void ResponseCache::add(std::string & file, Response * res) {
	_responses.insert(std::make_pair(file, res));
}

void ResponseCache::del(std::string & file) {
	std::map<std::string, Response *>::iterator it = _responses.find(file);
	if (it != _responses.end())
		delete (it->second);
}

Response * ResponseCache::get(std::string & file) {
	std::map<std::string, Response *>::iterator it = _responses.find(file);
	if (it != _responses.end())
		return (it->second);
	return (NULL);
}

ResponseCache::~ResponseCache() {
	for (std::map<std::string, Response *>::iterator it = _responses.begin();
		it != _responses.end(); it++) {
		delete (it->second);
	}
}
