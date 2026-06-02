#pragma once

#include <map>

class FileBodyCache {
public:
	static FileBodyCache	&get_instance();

	int					add(const std::string &filename);
	void				del(const std::string &filename);
	const std::string	&get(const std::string &filename) const;

private:
	FileBodyCache();
	FileBodyCache(const FileBodyCache &);
	~FileBodyCache() { };

	FileBodyCache &operator=(const FileBodyCache &);

	std::map<std::string, std::string>	_bodies;
}
