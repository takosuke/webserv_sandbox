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

	std::fstream	stream;
	char			*buffer;
	size_t			pos;
	size_t			size;
	size_t			capacity;

	std::vector<std::string>	headers;

	bool			_error;

	Response(const Response & other) { *this = other; };
	Response & operator=(const Response & other) { if (this == &other) return (*this); return (*this); };

	void	buffer_headers();
	void	buffer_file();

public:

	Response() { };
	Response(char *buf, size_t buffer_size);
	Response(char *buf, size_t buffer_size, const std::string & filename);
	~Response() { };

	bool	done() const;
	bool	error() const;

	void	set_buffer(char *buf) { buffer = buf; };
	void	set_file(const std::string &filename);

	/**	These functions add header fields to the internal vector for later
	 * 	writing to a buffer and then the socket.
	 * 	Since they work with strings and vectors they should be wrapped in a try
	 * 	block.
	 */ 
	void	add_status_line(const std::string & version, int status_code);
	void	add_header_field(const std::string & name, const std::string & value);
	void	add_content_length(const std::string &filepath);
	void	add_date();
	void	add_header_end();

	/**	These functions write the necessary parts of the Reponse to the buffer.
	 */ 
	void	clear_buffer();
	void	fill_buffer();

	int		construct_3xx(int code, const std::string &location);

	void	write_to(int fd);

};
