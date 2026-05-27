#pragma once

#include <map>
#include <vector>
#include <string>

#include <stdint.h>

#include "Config.hpp"
#include "Request.hpp"

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

struct Response {
private:
	static std::map<int, std::string>	reason_phrase_map;
	static void			init_reason_phrase_map();
	static std::string	get_reason_phrase(int code);

public:
	std::string		status_line;
	std::string		headers;
	std::string		entity;

	Response()
		: status_line(), headers("\r\n"), entity() {};
	Response(const Response & other) { *this = other; };
	~Response() { };

	Response & operator=(const Response & other);

	void	construct_status_line(const std::string & version, int status_code);
	void	add_header_field(const std::string & name, const std::string & value);
	void	add_content_length();

	void	print(std::ostream & out) const;
};

std::ostream & operator<<(std::ostream & out, const Response & res);

class ResponseStream {
public:
	ResponseStream();
	ResponseStream(const ResponseStream & other);
	ResponseStream(const Response & response);
	~ResponseStream();

	ResponseStream & operator=(const ResponseStream & other);

	void	response(const Response & res);

	bool	eof();

	int	write_to(int fd, size_t count);

private:
	const Response *	_response;
	const std::string *	_buf;
	size_t				_pos;
};
