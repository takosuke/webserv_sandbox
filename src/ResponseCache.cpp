#include "ResponseCache.hpp"

ResponseCache::ResponseCache() {
	_default = Response();
	_default.construct_status_line(HTTP_VERSION_STR, 500);
}

ResponseCache & ResponseCache::get_instance() {
	static ResponseCache	instance;
	return instance;
}

void ResponseCache::add(const std::string & file, Response * res) {
	_responses.insert(std::make_pair(file, res));
}

void ResponseCache::del(const std::string & file) {
	std::map<std::string, Response *>::iterator it = _responses.find(file);
	if (it != _responses.end())
		delete (it->second);
}

Response * ResponseCache::get(const std::string & file) {
	std::map<std::string, Response *>::iterator it = _responses.find(file);
	if (it != _responses.end())
		return (it->second);
	return (&_default);
}

ResponseCache::~ResponseCache() {
	for (std::map<std::string, Response *>::iterator it = _responses.begin();
		it != _responses.end(); it++) {
		delete (it->second);
	}
}
