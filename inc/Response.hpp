#pragma once

#include "Request.hpp"

#include <map>
#include <vector>

/* STATUS LINE ****************************************************************/

# ifndef HTTP_VERSION_STR
#  define HTTP_VERSION_STR "HTTP/1.0"
# endif

/* HEADER FIELD ***************************************************************/
/**
 *  General:
 * 	  - Date				(Should always be included by origin server)
 * 	  - Pragma				(Should always be sent if possible)
 *  Response:
 *    - Location			(Must for 3xx responses)
 *    - Server				(Not necessary)
 *    - WWW-Authenticate	(Out of scope)
 *  Entity:
 *    - Allow				(Can be sent with GET)
 *    - Content-Encoding	(Out of scope)
 *    - Content-Length		(Must be sent if an entity is sent)
 *    - Expires				(Out of scope)
 *    - Last-Modified		(Out of scope)
 */

/* RESPONSE *******************************************************************/

#define BYTES_PER_READ_CYCLE 1024
#define BYTES_PER_WRITE_CYCLE 1024

struct Response {
protected:
	static std::map<int, std::string>	reason_phrase_map;
	static void			init_reason_phrase_map();
	static std::string	get_reason_phrase(int code);

	const std::string *	_writebuf;
	size_t				_writepos;

	std::string		status_line;
	std::string		headers;
	std::string		entity;

public:
	Response() : _writebuf(&status_line), _writepos(0), status_line(), headers(), entity() {};
	Response(const Response & other) { *this = other; };
	~Response() {};

	Response & operator=(const Response & other) {
		if (this == &other)
			return (*this);
		_writebuf = &status_line;
		_writepos = 0;
		status_line = other.status_line;
		headers = other.headers;
		entity = other.entity;
		return (*this);
	};

	void	construct_status_line(const std::string & version, int status_code);
	void	add_header_field(const std::string & name, const std::string & value);


	int		write_count(int fd, size_t count);
};

std::ostream & operator<<(std::ostream & out, const Response & r);
