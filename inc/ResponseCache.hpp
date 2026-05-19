#pragma once

#include <map>

#include "Response.hpp"

class ResponseCache {
public:
	static ResponseCache &	get_instance();

	void		add(std::string & file, Response * res);
	void		del(std::string & file);
	Response *	get(std::string & file);

private:
	ResponseCache() { };
	ResponseCache(const ResponseCache &);
	~ResponseCache();

	ResponseCache & operator=(const ResponseCache &);

	std::map<std::string, Response *>	_responses;
};
