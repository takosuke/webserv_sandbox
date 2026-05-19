#pragma once

#include <set>

#include "FileConnection.hpp"

class FileLoop {
public:
	static FileLoop &	get_instance();

	void	add(FileConnection * conn);
	void	del(FileConnection * conn);
	void	run();

private:
	FileLoop() { };
	FileLoop(const FileLoop &);
	~FileLoop() { };

	void	clear();

	FileLoop & operator=(const FileLoop &);

	std::set<FileConnection *>	_files;
	std::set<FileConnection *>	_deletion_queue;
};
