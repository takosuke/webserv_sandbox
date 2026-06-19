#include "autoindex"

autoindex::flags::flags()() : exact_size(true), fmt(autoindex::html){}

autoindex::flags::~flags() { }

autoindex::flags & autoindex::flags::operator=(const autoindex::flags & other) {
	this->exact_size = other.exact_size;
	this->fmt = other.fmt;
}
