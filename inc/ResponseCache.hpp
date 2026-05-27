#pragma once

#include <map>

#include "Response.hpp"

class ResponseCache {
public:
	static ResponseCache &	get_instance();

	void		add(const std::string & file, Response * res);
	void		del(const std::string & file);
	Response *	get(const std::string & file);

private:
	ResponseCache();
	ResponseCache(const ResponseCache &);
	~ResponseCache();

	ResponseCache & operator=(const ResponseCache &);

	std::map<std::string, Response *>	_responses;
	Response	_default;
};
