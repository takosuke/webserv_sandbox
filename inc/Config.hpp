#pragma once

#include "ConfigParser.hpp"

#include <netinet/in.h>

#include <string>
#include <set>
#include <vector>
#include <map>
#include <iostream>

/* DIRECTIVE STRUCTS **********************************************************/

namespace config {
	/** Struct to hold information about a client request header */
	struct header {
	public:
		/** Sets the size of the buffer used for reading a client request
		 * header in bytes.
		 * If the request header is to big a large buffer can be used. */
		long unsigned int	buffer_size;
		/** Defines a timout for reading a client request header in seconds.
		 * If a client does not transmit the entire header within this time,
		 * the request is terminated with the 408 (Request Time-out) Error. */
		long unsigned int	timeout;
		/** Sets the number of large buffers for client request headers */
		long unsigned int	nlbuffers;
		/** Sets the size of large buffers for client request headers */
		long unsigned int	lbuffer_size;

		header();
		header(const header & other);
		~header();

		header & operator=(const header & other);
	};

	void add_client_header_buffer_size(config::header & header, const std::vector<Token> & tokens);
	void add_client_header_timeout(config::header & header, const std::vector<Token> & tokens);
	void add_large_client_header_buffers(config::header & header, const std::vector<Token> & tokens);

	/** Struct to hold information about a client requests body */
	struct body {
	public:
		/** The size of the buffer used for reading a client's body in bytes. */
		long unsigned int	buffer_size;
		/** The maximum size a client's body can have in bytes.
		 * If the body exceeds this size a 413 (Request Entitiy Too Large)
		 * Error is returned */
		long unsigned int	max_size;
		/** Sets the timout for reading a client request body in seconds.
		 * The timeout is only set for a period between successive read
		 * operations, not for reading the full request body. */
		long unsigned int	timeout;
	
		body();
		body(const body & other);
		~body();

		body & operator=(const body & other);
	};

	void add_client_body_buffer_size(config::body & body, const std::vector<Token> & tokens);
	void add_client_body_timeout(config::body & body, const std::vector<Token> & tokens);
	void add_client_max_body_size(config::body & body, const std::vector<Token> & tokens);
	
	/** Struct to hold information about buffer used for reading fiels from the
	 * disk */
	struct output {
	public:
		/** The size of the buffers used to reading from the disk */
		unsigned long int	buffer_size;
		/** The number of buffers available to read from the disk */
		unsigned long int	nbuffers;

		output();
		output(const output & other);
		~output();

		output & operator=(const output & other);
	};

	void add_output_buffers(config::output & output, const std::vector<Token> & tokens);

	/** Struct to hold information about allowed HTTP methods */
	struct limit {
	public:
		/** Enum of all HTTP methods. */
		enum method {
			GET, HEAD, POST, PUT, DELETE, MKCOL, COPY, MOVE, OPTIONS, PROPFIND,
			PROPPATCH, LOCK, UNLOCK, PATCH, INVALID
		};

		/** Set of all allowed methods. */
		std::set<limit::method> allowed;
	
		limit();
		limit(const limit & other);
		~limit();

		limit & operator=(const limit & other);

		/** Checks if a method is registered as allowed.
		 * First matches the string to a method with method_from_string()
		 * and passes the method to the is_allowed(method) function. */
		bool	is_allowed(const std::string & met) const;
		/** Checks if a method is registered as allowed. */
		bool	is_allowed(const method & met) const;

		/** Returns a method that matches the given string.
		 * If no matching method was found returns INVALID */
		static method	method_from_string(const std::string & str);

		/** Returns a string that matches the given method.
		 * If no matching method was found returns INVALID */
		static std::string string_from_method(const config::limit::method & method);

		/** Sets the set of allowed methods based on the tokens in a
		 * limit_except directive.
		 * Returns false if any of the tokens don't contain vaild method
		 * strings. */
		void	set_allowed(const std::vector<Token> & tokens);
	};

	void add_limit_except(config::limit & limit, const std::vector<Token> & Tokens);

	/** Struct to hold information about a port and addr a socket is listening
	 * on. 
	 * Can create a 'struct sockaddr' for use with bind(3) based on the set
	 * address and port. */
	struct listen {
	private:
		struct sockaddr_in	sockaddr;
	public:
		/** The Port in host-byte order */
		long unsigned int	port;
		/** The address in network-byte order */
		long unsigned int	addr;
		/** The backlog for a socket */
		long unsigned int	backlog;
		/** Bool that shows if a default_server parameter was found during
		 * construction through config_addr().
		 * This bool does not get copied during copy assignment. */
		bool				is_default;

		listen();
		listen(const listen & other);
		listen(long unsigned int p, long unsigned int a, long unsigned int b, bool is_d);
		~listen();

		listen & operator=(const listen & other);

		void	create_sockaddr();

		void	set_sockaddr(struct sockaddr_in & other) { sockaddr = other; };	

		const struct sockaddr_in &	get_sockaddr() const { return (sockaddr); };
	};

	void add_listen(std::vector<config::listen> & listenvec, const std::vector<Token> & tokens);

	/** Every location and server should be able to have their own mime/type map.
	 * So we just need a class to hold these types that also allows us to copy them
	 * over for other objects that inherit their config from the original one.
	 *
	 * The class is just using a std::map underneath as it should suffice for
	 * setting up the extension/type pairing.
	 *
	 * Since the type directive is a block directive we can easily copy the one of
	 * the encapsulating block by default and just create a new one based on a
	 * parameterized ctor that takes the block directive as an argument.
	 */
	struct mime {
	private:
		std::string _default;
		/** Map of extension to type */
		std::map<std::string, std::string> types;

	public:
		mime();
		mime(const mime & other);
		~mime();

		mime & operator=(const mime & other);

		void	clear_types();
		void	add_type(const std::string & extension, const std::string & type);
		void	set_default(const std::string & type);

		const std::string &	get_type(const std::string & extension) const;
		const std::string & get_default() const;

		const std::map<std::string, std::string> & get_types() const;

		std::string get_type_ext_string() const;
	};

	void add_types(config::mime & mime, const BodyDirective & directive);
	void add_default_type(config::mime & mime, const std::vector<Token> & tokens);

	/** @brief Basic struct that holds information about a page associated to a
	 *  given error code. Since the code should be supplied by the user it
	 *  doesn't need to be saved here.
	 *	
	 *	@param response_code The code to be used in the response. If the code is
	 *	`-1` the code given by the internal redirection should be used.
	 */
	struct errorpageinfo {
	public:
		bool		internal;
		int			response_code;
		std::string	pagename;

		errorpageinfo();
		errorpageinfo(bool is_internal, int code, const std::string & p);
		errorpageinfo(const errorpageinfo &other) { *this = other; };
		~errorpageinfo();

		errorpageinfo & operator=(const errorpageinfo & other);
	};

#define DEFAULT_ERROR_PAGE "error_page.html"
#define DEFAULT_ERROR_CODE 404

	struct errors {
	private:
		config::errorpageinfo					_default;
		std::map<int, config::errorpageinfo>	_pagemap;

	public:
		errors();
		errors(const errors & other);
		~errors();

		errors & operator=(const errors & other);

		void		clear();

		void		add_page(int error_code, const config::errorpageinfo &epi);
	
		void		set_default(const config::errorpageinfo &epi);

		const config::errorpageinfo	&get_page(int error_code) const;
		const config::errorpageinfo	&get_default() const;

		void		stream_pages(std::ostream &out) const;
	};

	void add_error_page(config::errors & errors, const std::vector<Token> & tokens);

	void add_root(std::string & root, const std::vector<Token> & tokens);
	void add_server_name(std::vector<std::string> & names, const std::vector<Token> & tokens);

	void check_parameter_count(int min, int max, int size);

	struct cgi {
	public:
		std::string								pass;
		std::vector<std::pair<std::string, std::string> >	params;

		cgi();
		cgi(const cgi & other);
		~cgi();

		cgi & operator=(const cgi & other);
	};

	void add_cgi_pass(std::string & pass, const std::vector<Token> & tokens);
	void add_cgi_param(config::cgi & cgi, const std::vector<Token> & tokens);
}

std::ostream & operator<<(std::ostream & out, const config::header & header);
std::ostream & operator<<(std::ostream & out, const config::body & body);
std::ostream & operator<<(std::ostream & out, const config::output & output);
std::ostream & operator<<(std::ostream & out, const config::limit & limit);
std::ostream & operator<<(std::ostream & out, const config::listen & listen);
std::ostream & operator<<(std::ostream & out, const config::mime & mime);
std::ostream & operator<<(std::ostream & out, const config::errorpageinfo & errorpage);
std::ostream & operator<<(std::ostream & out, const config::errors & errors);
std::ostream & operator<<(std::ostream & out, const config::cgi & cgi);

bool operator==(const config::listen & lhs, const config::listen & rhs);

template <class T, template <class, class> class C>
void copy_deep_container(C<T *, typename std::allocator<T *> > & d,
		C<T *, typename std::allocator<T *> > & c) {
	for (typename C<T *, typename std::allocator<T *> >::const_iterator it = d.begin();
			it != d.end(); it++) {
		delete *it;
	}
	d.resize(0);
	for (typename C<T *, typename std::allocator<T *> >::const_iterator it = c.begin();
			it != c.end(); it++) {
		d.push_back(new T(*it));
	}
}

template <class T, template <class, class> class C>
void copy_deep_container(C<T *, typename std::allocator<T *> > & d,
		const C<T *, typename std::allocator<T *> > & c) {
	for (typename C<T *, typename std::allocator<T *> >::const_iterator it = d.begin();
			it != d.end(); it++) {
		delete *it;
	}
	d.resize(0);
	for (typename C<T *, typename std::allocator<T *> >::const_iterator it = c.begin();
			it != c.end(); it++) {
		d.push_back(new T(**it));
	}
}

/* LOCATION *******************************************************************/

class Server;

class Location {
private:
	std::string		path;

	std::string		root;

	config::limit	limit;
	config::body	body;
	config::output	output;
	config::mime	mime;
	config::errors	errorpages;
	config::cgi		cgi;

	std::vector<const Location *>	locations;

	void	delete_locations();

public:
	bool			is_prefix;

	Location();
	Location(const Location & other);
	Location(const BodyDirective & directive);
	Location(const Server & server);
	~Location();

	Location & operator=(const Location & other);

	void	from_directive(const BodyDirective & directive);
	void	from_server(const Server & server);

	const Location &	get_location(const std::string & uri) const;

	void	set_path(const std::string & other) { path = other; };
	void	set_root(const std::string & other) { root = other; };
	void	set_limit(const config::limit & other) { limit = other; };
	void	set_body(const config::body & other) { body = other;};
	void	set_output(const config::output & other) { output = other; };
	void	set_mime(const config::mime & other) { mime = other; };
	void	set_errorpages(const config::errors & other) { errorpages = other; };
	void	set_locations(const std::vector<const Location *> & other) { locations = other; };

	const std::string		& get_path() const { return (path); };
	const std::string		& get_root() const { return (root); };
	const config::limit		& get_limit() const { return (limit); };
	const config::body		& get_body() const { return (body); };
	const config::output	& get_output() const { return (output); };
	const config::mime		& get_mime() const { return (mime); };
	const config::errors	& get_errorpages() const { return (errorpages); };
	const config::cgi		& get_cgi() const { return (cgi); };
	const std::vector<const Location *>	& get_locations() const { return (locations); };

	int cache_errorpages() const;
};

std::ostream & operator<<(std::ostream & out, const Location & loc);

/* SERVER *********************************************************************/

class Http;

class Server {
private:
	std::string 				root;
	std::vector<std::string>	names;

	std::vector<config::listen>	listen;
	config::header				header;
	config::body				body;
	config::output				output;
	config::mime				mime;
	config::errors				errorpages;

	std::vector<const Location *> locations;

	void	delete_locations();

public:
	Server();
	Server(const Server & other);
	Server(const BodyDirective & directive);
	Server(const Http & http);
	~Server();

	Server & operator=(const Server & other);

	void	from_directive(const BodyDirective & directive);
	void	from_http(const Http & http);

	const Location &	get_location(const std::string & uri) const;

	void	set_root(const std::string & other) { root = other; }; 
	void	set_names(const std::vector<std::string> & other) { names = other; };
	void	set_listen(const std::vector<config::listen> & other) { listen = other; };
	void	set_header(const config::header & other) { header = other; };
	void	set_body(const config::body & other) { body = other; };
	void	set_output(const config::output & other) { output = other; };
	void	set_mime(const config::mime & other) { mime = other;  };
	void	set_errorpages(const config::errors & other) { errorpages = other; };
	void	set_locations(const std::vector<const Location *> & other) { copy_deep_container(locations, other); };

	const std::string					& get_root() const { return (root); };
	const std::vector<std::string>		& get_names() const { return (names); };
	const std::vector<config::listen>	& get_listen() const { return (listen); };
	const config::header				& get_header() const { return (header); };
	const config::body					& get_body() const { return (body); };
	const config::output				& get_output() const { return (output); };
	const config::mime					& get_mime() const { return (mime); };
	const config::errors				& get_errorpages() const { return (errorpages); };
	const std::vector<const Location *>			& get_locations() const { return (locations); };
};

std::ostream & operator<<(std::ostream & out, const Server & server);

/* PORT ***********************************************************************/

/* The Port class is mainly an interface to sort servers by their address:port
 * pairs and holds a map of the servers associated by their server name.
*/
class Port {
private:
	config::listen					listen;
	const Server *					dserver;
	bool							default_set;
	std::map<std::string, const Server *>	servername_map;

public:
	Port();
	Port(const Port & other);
	Port(const config::listen & other);
	~Port();

	Port & operator=(const Port & other);

	void	add_server(const Server & server);

	const Server &	get_server_by_name(const std::string & name) const;

	void	set_listen(const config::listen & other) { listen = other; };
	void	set_dserver(const Server * other) { dserver = other; };
	void	set_servername_map(const std::map<std::string, const Server *> & other) {servername_map = other; };

	const config::listen			& get_listen() const { return (listen); };
	const Server					* get_dserver() const { return (dserver); };
	const std::map<std::string, const Server *>	& get_servername_map() const { return (servername_map); };
};

/* HTTP ***********************************************************************/

class Http {
private:
	std::string		root;

	config::header	header;
	config::body	body;
	config::output	output;
	config::mime	mime;
	config::errors	errorpages;

	std::map<struct sockaddr_in, Port>		ports;
	std::vector<const Server *>	servers;

	void	delete_servers();

public:
	Http();
	Http(const Http & other);
	Http(const BodyDirective & directive);
	~Http();

	Http & operator=(const Http & other);

	void	from_directive(const BodyDirective & directive);

	const Server &	get_server(const struct sockaddr_in & sockaddr, const std::string & name) const;

	void	set_root(const std::string & other) { root = other; };
	void	set_header(const config::header & other) { header = other; };
	void	set_body(const config::body & other) { body  = other; };
	void	set_output(const config::output & other) { output = other; };
	void	set_mime(const config::mime & other) { mime = other; };
	void	set_errorpages(const config::errors & other) { errorpages = other; };
	void	set_servers(const std::vector<const Server *> & other) { copy_deep_container(servers, other); };

	const std::string			& get_root() const { return (root); };
	const config::header		& get_header() const { return (header); };
	const config::body			& get_body() const { return (body); };
	const config::output		& get_output() const { return (output); };
	const config::mime			& get_mime() const { return (mime); };
	const config::errors		& get_errorpages() const { return (errorpages); };
	const std::vector<const Server *>			& get_server() const { return (servers); };
	const std::map<struct sockaddr_in, Port>	& get_ports() const { return (ports); };
};

std::ostream & operator<<(std::ostream & out, const Http & http);
