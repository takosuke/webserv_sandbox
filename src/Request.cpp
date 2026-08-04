#include "Request.hpp"

#include "Logger.hpp"

#include <cctype>
#include <algorithm>

Request::Request()
	: method(UNKNOWN), content_length(0), port(-1), status(0),
	internal(true), no_file(false) {

}

