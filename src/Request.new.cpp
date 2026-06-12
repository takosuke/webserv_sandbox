#include "Request.new.hpp"

#include "Logger.hpp"

#include <cctype>
#include <algorithm>

Request::Request()
	: method(UNKOWN), content_length(0), status(0) {

}

