#pragma once

#include "Config.hpp"

#include <map>
#include <vector>
#include <string>

#include <stdint.h>

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

/**	@brief The Response class is an Interface for a character buffer. It
 * 	implements functions to created the proper status line, headers and read to
 * 	the buffer from a file.
 *
 * 	The Response class itself should not be used to write to a client but just
 * 	to get the necessary data to send to the Client.
 */
struct Response {
private:
	static std::map<int, std::string>	reason_phrase_map;
	static int					init_reason_phrase_map();
	static const std::string	&get_reason_phrase(int code);

public:
	std::vector<std::string>	headers;

	Response() { };
	~Response() { };

	Response & operator=(const Response & other);

	/**	These functions add header fields to the internal vector for later
	 * 	writing to a buffer and then the socket.
	 * 	Since they work with strings and vectors they should be wrapped in a try
	 * 	block.
	 */ 
	void	add_status_line(const std::string & version, int status_code);
	void	add_header_field(const std::string & name, const std::string & value);
	void	add_header_field(const std::string & name, size_t num);
	void	add_allowed(const Location *loc);
	void	add_date();
	void	add_header_end();
};
