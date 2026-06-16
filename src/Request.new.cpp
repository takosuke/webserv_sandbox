#include "Request.new.hpp"

#include "Logger.hpp"

#include <cctype>
#include <algorithm>

Request::Request()
	: method(UNKNOWN), content_length(0), status(0),
	interal(true), no_file(false) {

}

