#include "FileLoop.hpp"

FileLoop & FileLoop::get_instance() {
	static FileLoop	instance;
	return (instance);
}

void FileLoop::add(FileConnection * conn) {
	_files.insert(conn);
}

void FileLoop::del(FileConnection * conn) {
	_deletion_queue.insert(conn);
}

void FileLoop::run() {
	for (std::set<FileConnection *>::iterator it = _files.begin();
		it != _files.end(); it++) {
		(*it)->handle();
	}
	clear();
}

void FileLoop::clear() {
	for (std::set<FileConnection *>::iterator it = _deletion_queue.begin();
		it != _deletion_queue.end(); it++) {
		_files.erase(*it);
	}
}
