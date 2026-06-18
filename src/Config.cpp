#include "Config.hpp"

#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
#include <sstream>
#include <map>
#include <utility>
#include <vector>
#include <iostream>

const static std::string methodstrings[4] = {
	"GET", "POST", "DELETE", "UNKNOWN"
};
const static HttpMethod methodarr[15] = {
	GET,
	POST,
	DELETE,
	UNKNOWN
};

HttpMethod method_from_string(const std::string & str) {
	for (int i = 0; i < 3; i++) {
		if (str == methodstrings[i])
			return (methodarr[i]);
	}
	return (UNKNOWN);
}

std::string string_from_method(const HttpMethod & method) {
	for (int i = 0; i < 3; i++) {
		if (method == methodarr[i])
			return (methodstrings[i]);
	}
	return (methodstrings[3]);
}

/* DIRECTIVE STRUCTS **********************************************************/

/* CONFIG :: HEADER ***********************************************************/

config::header::header() {
	buffer_size = 1024;
	timeout = 60;
	nlbuffers = 4;
	lbuffer_size = 8192;
}

config::header::header(const header & other) {
	*this = other;
}

config::header::~header() { };

config::header & config::header::operator=(const header & other) {
	if (this == &other)
		return (*this);
	buffer_size = other.buffer_size;
	timeout = other.timeout;
	nlbuffers = other.nlbuffers;
	lbuffer_size = other.lbuffer_size;
	return (*this);
}

void config::add_client_header_buffer_size(config::header & header, const std::vector<Token> & tokens) {
	try {
		check_parameter_count(1, 1, tokens.size());

		if (tokens[0].type != Token::number && tokens[0].type != Token::memory)
			throw (std::runtime_error("expected number/memory parameter"));

		header.buffer_size = tokens[0].num;
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[client_header_buffer_size] ") + e.what()));
	}
}

void config::add_client_header_timeout(config::header & header, const std::vector<Token> & tokens) {
	try {
		check_parameter_count(1, 1, tokens.size());

		if (tokens[0].type != Token::number && tokens[0].type != Token::time)
			throw (std::runtime_error("expected number/time parameter"));

		header.timeout = tokens[0].num;
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[client_header_timeout] ") + e.what()));
	}
}

void config::add_large_client_header_buffers(config::header & header, const std::vector<Token> & tokens) {
	try {
		check_parameter_count(1, 2, tokens.size());

		if (tokens[0].type != Token::number)
			throw (std::runtime_error("expected number parameter"));
		header.nlbuffers = tokens[0].num;

		if (tokens[1].type != Token::number && tokens[1].type != Token::memory)
			throw (std::runtime_error("expected number/memory parameter"));
		header.lbuffer_size = tokens[1].num;
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[large_client_header_buffers] ") + e.what()));
	}
}

std::ostream & operator<<(std::ostream & out, const config::header & header) {
	out << "header { buffer_size: " << header.buffer_size <<
		", timeout: " << header.timeout <<
		", large_buffers: " << header.nlbuffers <<
		", large_buffer_size: " << header.lbuffer_size << " }";
	return (out);
}

/* CONFIG :: BODY *************************************************************/

config::body::body() {
	buffer_size = 8192;
	max_size = 1048576;
	timeout = 60;
}

config::body::body(const body & other) {
	*this = other;
}

config::body::~body() { }

config::body & config::body::operator=(const body & other) {
	if (this == &other)
		return (*this);
	buffer_size = other.buffer_size;
	max_size = other.max_size;
	timeout = other.timeout;
	return (*this);
}

void config::add_client_body_buffer_size(config::body & body, const std::vector<Token> & tokens) {
	try {
		check_parameter_count(1, 1, tokens.size());
		if (tokens[0].type != Token::number && tokens[0].type != Token::memory)
			throw (std::runtime_error("expected numerical parameter"));
		body.buffer_size = tokens[0].num;
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[client_body_buffer_size] ") + e.what()));
	}
}

void config::add_client_body_timeout(config::body & body, const std::vector<Token> & tokens) {
	try {
		check_parameter_count(1, 1, tokens.size());
		if (tokens[0].type != Token::number && tokens[0].type != Token::time)
			throw (std::runtime_error("expected numerical parameter"));
		body.timeout = tokens[0].num;
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[client_body_timeout] ") + e.what()));
	}
}

void config::add_client_max_body_size(config::body & body, const std::vector<Token> & tokens) {
	try {
		check_parameter_count(1, 1, tokens.size());

		if (tokens[0].type != Token::number && tokens[0].type != Token::memory)
			throw (std::runtime_error("expected numerical parameter"));

		body.max_size = tokens[0].num;
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[client_max_body_size] ") + e.what()));
	}
}

std::ostream & operator<<(std::ostream & out, const config::body & body) {
	out << "body { buffer_size: " << body.buffer_size <<
		", max_size: " << body.max_size <<
		", timeout: " << body.timeout << " }";
	return (out);
}

/* CONFIG :: OUTPUT ***********************************************************/

config::output::output() {
	buffer_size = 32000;
	nbuffers = 2;
}

config::output::output(const config::output & other) {
	*this = other;
}

config::output::~output() {

}

config::output & config::output::operator=(const config::output & other) {
	if (this == &other)
		return (*this);
	buffer_size = other.buffer_size;
	nbuffers = other.nbuffers;
	return (*this);
}

void config::add_output_buffers(config::output & output, const std::vector<Token> & tokens) {
	try {
		check_parameter_count(2, 2, tokens.size());

		if (tokens[0].type != Token::number)
			throw (std::runtime_error("expected number parameter"));
		output.buffer_size = tokens[0].num;

		if (tokens[1].type != Token::number)
			throw (std::runtime_error("expected number parameter"));
		output.nbuffers = tokens[1].num;
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[output_buffers] ") + e.what()));
	}
}

std::ostream & operator<<(std::ostream & out, const config::output & output) {
	out << "output { buffer_size: " << output.buffer_size <<
		", buffers: " << output.nbuffers << " }";
	return (out);
}

/* CONFIG :: LIMIT ************************************************************/

config::limit::limit() {
	allowed.insert(GET);
	allowed.insert(POST);
	allowed.insert(DELETE);
}

config::limit::limit(const config::limit & other) {
	*this = other;
}

config::limit::~limit() { };

config::limit & config::limit::operator=(const config::limit & other) {
	if (this == &other)
		return (*this);
	allowed = other.allowed;
	return (*this);
}

bool config::limit::is_allowed(const std::string & met) const {
	return (is_allowed(method_from_string(met)));
}

bool config::limit::is_allowed(const HttpMethod & met) const {
	std::set<HttpMethod>::const_iterator it = allowed.find(met);

	return (it != allowed.end());
}

void config::limit::set_allowed(const std::vector<Token> & tokens) {
	if (tokens.size() == 0)
		 throw (std::runtime_error("no parameteres provided for limit_except directive"));

	allowed = std::set<HttpMethod>();

	for (std::vector<Token>::const_iterator it = tokens.begin();
		it != tokens.end(); it++) {
		if (it->type != Token::string)
			throw (std::runtime_error("non string token provided as a parameter"));
		HttpMethod tmp = method_from_string(it->str);
		if (tmp == UNKNOWN)
			throw (std::runtime_error("non method string provided as a parameter"));
		allowed.insert(tmp);
	}
}

void config::add_limit_except(config::limit & limit, const std::vector<Token> & tokens) {
	try {
		limit.set_allowed(tokens);
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[limit_except] ") + e.what()));
	}
}

std::ostream & operator<<(std::ostream & out, const config::limit & limit) {
	out << "limit_except { allowed: ";
	if (limit.allowed.size() == 0) {
		out << "all }";
		return (out);
	}
	for (std::set<HttpMethod>::const_iterator it = limit.allowed.begin();
		it != limit.allowed.end(); it++) {
		if (it != limit.allowed.begin())
			out << ", ";
		out << string_from_method(*it);
	}
	out << " }";
	return (out);
}

/* CONFIG :: LISTEN ***********************************************************/

config::listen::listen() {
	port = 80;
	addr = 16777343; /** 127.0.0.1 in network-byte order */
	backlog = 5;
	is_default = false;
	create_sockaddr();
}

config::listen::listen(const listen & other) {
	*this = other;
}

config::listen::listen(unsigned long int p, unsigned long int a, unsigned long int b, bool is_d) {
	port = p;
	addr = a;
	backlog = b;
	is_default = is_d;
	create_sockaddr();
}

config::listen & config::listen::operator=(const config::listen & other) {
	port = other.port;
	addr = other.addr;
	backlog = other.backlog;
	is_default = other.is_default;
	create_sockaddr();
	return (*this);
}

config::listen::~listen() { };

#include <cstring>

void config::listen::create_sockaddr() {
	bzero(&sockaddr, sizeof(sockaddr));
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(port);
	sockaddr.sin_addr.s_addr = static_cast<in_addr_t>(addr);
}

void config::add_listen(std::vector<config::listen> & listenvec, const std::vector<Token> & tokens) {
	try {
		check_parameter_count(1, 3, tokens.size());

		config::listen listen;

		std::vector<Token>::const_iterator	tokenit = tokens.begin();

		unsigned long int		tmpaddr = listen.addr;
		char *	tmp = reinterpret_cast<char *>(&tmpaddr);
		int		num;

		std::istringstream	stream(tokenit->str);
		if (std::isdigit(stream.peek())) {
			for (int i = 0; i < 4; i++) {
				stream >> num;
				if (stream.bad())
					throw (std::runtime_error("istream error reading address"));
				tmp[i] = static_cast<char>(num);
				if (i != 3 && stream.eof()) {
					if (i == 0) {
						stream.seekg(0);
						tmpaddr = listen.addr;
						break ;
					}
					throw (std::runtime_error("address formatting error"));
				}
				if (i < 3) {
					if (stream.peek() == '.')
						stream.ignore(1, '.');
					else
						throw (std::runtime_error("address formatting error"));
				}
			}
		}
		listen.addr = tmpaddr;
		if (stream.peek() == ':') {
			stream.ignore(1, ':');
		}
		if (!stream.eof()) {
			stream >> listen.port;
			if (stream.bad())
				throw (std::runtime_error("istream error reading port"));
		}
		++tokenit;

		listen.is_default = false;
		if (tokenit != tokens.end()) {
			if (tokenit->type != Token::string)
				throw (std::runtime_error("non-string token as parameter"));
			if (tokenit->str == "default_server") {
				listen.is_default = true;
				++tokenit;
			}
		}

		if (tokenit != tokens.end()) {
			if (tokenit->type != Token::string && tokenit->type != Token::path)
				throw (std::runtime_error("non-string token as parameter"));
			size_t	seppos = tokenit->str.find('=');

			if (seppos == std::string::npos)
				throw (std::runtime_error("expected to find a '=' variable/value seperator"));
			if (!tokenit->str.compare(0, seppos, "backlog")) {
				std::istringstream stream(tokenit->str.substr(seppos + 1, tokenit->str.size() - seppos - 1));

				stream >> listen.backlog;
				if (stream.bad())
					throw (std::runtime_error("expected number after backlog parameter"));
			}
			++tokenit;
		}

		if (tokenit != tokens.end())
			throw (std::runtime_error("too many parameters for listen directive"));

		listen.create_sockaddr();
		listenvec.push_back(listen);
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[listen] ") + e.what()));
	}
}

std::ostream & operator<<(std::ostream & out, const config::listen & listen) {
	out << "listen { ";
	if (listen.is_default)
		out << "default_server, ";
	int	cpy_addr = listen.addr;
	char *tmp = reinterpret_cast<char *>(&cpy_addr);

	out << "address: " << static_cast<int>(tmp[0]) <<
		"." << static_cast<int>(tmp[1]) <<
		"." << static_cast<int>(tmp[2]) <<
		"." << static_cast<int>(tmp[3]) <<
		", port: " << listen.port <<
		", backlog: " << listen.backlog << " }";
	return (out);
}

bool operator==(const config::listen & lhs, const config::listen & rhs) {
	return (lhs.port == rhs.port &&
		lhs.addr == rhs.addr &&
		lhs.backlog == rhs.backlog);
}

/* CONFIG :: MIME *************************************************************/

config::mime::mime() {
	_default = std::string("text/plain");
	add_type("html", "text/html");
	add_type("gif", "image/gif");
	add_type("jpeg", "image/jpeg");
}

config::mime::mime(const mime & other) {
	*this = other;
}

config::mime::~mime() { };

config::mime & config::mime::operator=(const mime & other) {
	if (this == &other)
		return (*this);
	_default = other._default;
	types = other.types;
	return (*this);
}

void config::mime::clear_types() {
	types.clear();
}

void config::mime::add_type(const std::string & extension, const std::string & type) {
	types[extension] = type;
}

void config::mime::set_default(const std::string & type) {
	_default = type;
}

const std::string & config::mime::get_type(const std::string & extension) const {
	std::map<std::string, std::string>::const_iterator it = types.find(extension);

	if (it == types.end())
		return (_default);
	return (it->second);
}

const std::string & config::mime::get_default() const{
	return (_default);
}

void config::add_types(config::mime & mime, const BodyDirective & directive) {
	try {
		mime.clear_types();

		if (directive.body_directives.size() > 0)
			throw (std::runtime_error("No body directives allowed inside types directive"));

		for (std::vector<SimpleDirective>::const_iterator it = directive.simple_directives.begin();
		it != directive.simple_directives.end(); it++) {
			check_parameter_count(1, -1, directive.simple_directives.size());

			for (std::vector<Token>::const_iterator tokit = it->parameters.begin();
			tokit != it->parameters.end(); tokit++) {
				if (tokit->type != Token::string)
					throw (std::runtime_error("expected string token as parameter for type extension"));
				else
					mime.add_type(tokit->str, it->name);
			}
		}
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[types] ") + e.what()));
	}
}

std::string config::mime::get_type_ext_string() const {
	std::vector<std::pair<std::string, std::vector<std::string> > > collect_types;

	for (std::map<std::string, std::string>::const_iterator it = types.begin();
		it != types.end(); it++) {
		std::vector<std::pair<std::string, std::vector<std::string> > >::iterator typeit;
		for (typeit = collect_types.begin();
			typeit != collect_types.end(); typeit++) {
			if (typeit->first == it->second) {
				typeit->second.push_back(it->first);
				break ;
			}
		}
		if (typeit == collect_types.end()) {
			std::pair<std::string, std::vector<std::string> > new_type = std::make_pair(it->second, std::vector<std::string>());
			new_type.second.push_back(it->first);
			collect_types.push_back(new_type);
		}
	} 

	std::ostringstream ret;
	ret << "type_extension_map { ";
	for (std::vector<std::pair<std::string, std::vector<std::string> > >::const_iterator it = collect_types.begin();
		it != collect_types.end(); it++) {
		if (it != collect_types.begin())
			ret << ", ";
		ret << it->first << " extensions { ";
		for (std::vector<std::string>::const_iterator extit = it->second.begin();
			extit != it->second.end(); extit++) {
			if (extit != it->second.begin())
				ret << ", ";
			ret << *extit;
		}
		ret << " }";
	}
	ret << " }";
	return (ret.str());
}

void config::add_default_type(config::mime & mime, const std::vector<Token> & tokens) {
	try {
		check_parameter_count(1, 1, tokens.size());
		if (tokens[0].type != Token::string)
			throw (std::runtime_error("expected string"));
		mime.set_default(tokens[0].str);
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[default_type] ") + e.what()));
	}
}

std::ostream & operator<<(std::ostream & out, const config::mime & mime) {
	out << "mime { default_type: " << mime.get_default() <<
		", types: " << mime.get_type_ext_string() << " }";
	return (out);
}

/* CONFIG :: REDIRECT *********************************************************/

config::redirect::redirect()
	: is_set(false), status_code(0), path("") {

}

config::redirect::redirect(const config::redirect &other) {
	*this = other;
}

config::redirect::~redirect() {

}

config::redirect &config::redirect::operator=(const config::redirect &other) {
	if (this == &other)
		return (*this);
	is_set = other.is_set;
	status_code = other.status_code;
	path = other.path;
	return (*this);
}

void config::add_return(config::redirect &redirect, const std::vector<Token> &tokens) {
	try {
		check_parameter_count(1, 2, tokens.size());

		std::vector<Token>::const_iterator it = tokens.begin();

		if (it->type == Token::number) {
			redirect.status_code = it->num;
			if (redirect.status_code < 0 || redirect.status_code >= 600)
				throw (std::runtime_error("invalid status code"));
			++it;
		}
		
		if (it != tokens.end() && !(it->type == Token::string
			|| it->type == Token::path || it->type == Token::url))
			throw (std::runtime_error("expected string, path or url"));

		redirect.path = it->str;
		redirect.is_set = true;
	} catch (std::exception &e) {
		throw (std::runtime_error(std::string("[return] ") + e.what()));
	}
}

/* CONFIG :: ERRORS ***********************************************************/

config::errors::errors() {
	_default = config::errorpageinfo(true, 404, DEFAULT_ERROR_PAGE);
}

config::errors::errors(const config::errors & other) {
	*this = other;
}

config::errors::~errors() { }

config::errors & config::errors::operator=(const config::errors & other) {
	if (this == &other)
		return (*this);
	_default = other._default;
	_pagemap = other._pagemap;
	return (*this);
}

void config::errors::clear() {
	_pagemap.clear();
}

void config::errors::add_page(int error_code, const config::errorpageinfo &epi) {
	_pagemap[error_code] = epi;
}

void config::errors::set_default(const config::errorpageinfo &epi) {
	_default = epi;
}

bool config::errors::has_page(int error_code) const {
	std::map<int, config::errorpageinfo>::const_iterator it = _pagemap.find(error_code);
	
	return (it != _pagemap.end());
}

/** TODO: check if it should really return the default page */
const config::errorpageinfo & config::errors::get_page(int error_code) const {
	std::map<int, config::errorpageinfo>::const_iterator it = _pagemap.find(error_code);

	if (it == _pagemap.end())
		return (_default);
	return (it->second);
}

const config::errorpageinfo & config::errors::get_default() const {
	return (_default);
}

void config::errors::stream_pages(std::ostream &out) const {
	for (std::map<int, config::errorpageinfo>::const_iterator it = _pagemap.begin();
		it != _pagemap.end(); it++) {
	  	if (it != _pagemap.begin())
			out << ", ";
		out << "{ code: " << it->first
			<< ", " << it->second << " } ";
	}
}

config::errorpageinfo::errorpageinfo() : internal(true), response_code(0), pagename() { }

config::errorpageinfo::errorpageinfo(bool is_internal, int code, const std::string & p) {
	internal = is_internal;
	response_code = code;
	pagename = p;
}

config::errorpageinfo::~errorpageinfo() { }

config::errorpageinfo & config::errorpageinfo::operator=(const config::errorpageinfo & other) {
	if (this == &other)
		return (*this);
	internal = other.internal;
	response_code = other.response_code;
	pagename = other.pagename;
	return (*this);
}

bool config::starts_with_scheme(const std::string & uri) {
	static std::string	schemestrs[14] = {
		std::string("blob"),
		std::string("data"),
		std::string("file"),
		std::string("ftp"),
		std::string("http"),
		std::string("https"),
		std::string("javascript"),
		std::string("mailto"),
		std::string("ssh"),
		std::string("tel"),
		std::string("urn"),
		std::string("view-source"),
		std::string("ws"),
		std::string("wss")
	};
	for (int i = 0; i < 14; i++) {
		size_t	schemelen = schemestrs[i].size();
		if (uri.compare(0, schemelen, schemestrs[i]) == 0
			&& uri.compare(schemelen, 3, "://") == 0) {
			return (true);
		}
	}
	return (false);
}

void config::add_error_page(config::errors & errors, const std::vector<Token> & tokens) {
	try {
		check_parameter_count(2, -1, tokens.size());

		std::vector<Token>::const_iterator it = tokens.begin();

		std::vector<int>	codes;

		while (it != tokens.end()) {
			if (it->type != Token::number)
				break ;
			codes.push_back(it->num);
			++it;
		}

		if (codes.size() == 0)
			throw (std::runtime_error("no error codes provided"));

		int		response_code = 0;
		if (it != tokens.end() && it->type == Token::string && it->str == "=") {
			response_code = -1;
			++it;
		}

		if (it != tokens.end() && it->type == Token::string && it->str[0] == '=') {
			if (response_code == -1)
				throw (std::runtime_error("cgi and specific response in same directive"));

			// WARNING
			/** Potentially we need to check later if a location is valid as a
		* cgi response code location based on if it has a cgi_pass directive */
			try {
				std::istringstream stream(it->str.substr(1, it->str.size() - 1));
				stream >> response_code;
				if (stream.bad() || !stream.eof())
					throw (std::runtime_error("invalid number after response code specifier '='"));
			} catch(std::exception & e) {
				throw (std::runtime_error(std::string("exception caught trying to read response code specifier: ") + e.what()));;
			}
			++it;
		}

		if (it == tokens.end())
			throw (std::runtime_error("no error page provided"));
		if (it->type != Token::path && it->type != Token::string)
			throw (std::runtime_error("expected path or string token"));

		std::string path = it++->str;
		if (path.size() == 0)
			throw (std::runtime_error("no error page provided"));

		if (it != tokens.end())
			throw (std::runtime_error("too many parameters for error_page directive"));
	
		bool	internal = true;
		if (config::starts_with_scheme(path)) {
			internal = false;
			if (response_code == 0)
				response_code = 302;
			else if (response_code < 300 || response_code >= 400)
				throw (std::runtime_error("invalid response code overwrite for url. use 3xx class codes"));
		}

		try {
			if (response_code == 0) {
				for (std::vector<int>::const_iterator codeit = codes.begin();
				codeit != codes.end(); codeit++) {
					errors.add_page(*codeit, config::errorpageinfo(internal, *codeit, path));
				}
			} else {
				for (std::vector<int>::const_iterator codeit = codes.begin();
				codeit != codes.end(); codeit++) {
					errors.add_page(*codeit, config::errorpageinfo(internal, response_code, path));
				}
			}
		} catch (std::exception & e) {
			throw (std::runtime_error(std::string("exception caught adding errorpages: ") + e.what()));
		}
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[error_pages] ") + e.what()));
	}
}

std::ostream & operator<<(std::ostream & out, const config::errorpageinfo & errorpage) {
	out << "errorpage { response_code: " << errorpage.response_code
		<< ", page: " << errorpage.pagename << " }";
	return (out);
}

std::ostream & operator<<(std::ostream & out, const config::errors & errors) {
	out << "errors { default: " << errors.get_default() <<
		" }, pages { ";
	errors.stream_pages(out);
	out << " }";
	return (out);
}

/* CONFIG :: CGI **************************************************************/

config::cgi::cgi() : is_set(false), pass("") { };

config::cgi::cgi(const config::cgi &other) { *this = other; };

config::cgi::~cgi() { };

config::cgi &config::cgi::operator=(const config::cgi &other) {
	if (this == &other)
		return (*this);
	is_set = other.is_set;
	pass = other.pass;
	params = other.params;
	return (*this);
}

void config::add_cgi_pass(config::cgi &cgi, const std::vector<Token> &tokens) {
	try {
		check_parameter_count(1, 1, tokens.size());

		if (tokens.front().type != Token::path
			&& tokens.front().type != Token::url
			&& tokens.front().type != Token::string)
			throw (std::runtime_error("expected path, url or string"));
		cgi.pass = tokens.front().str;
		cgi.is_set = true;
	} catch (std::exception &e) {
		throw (std::runtime_error(std::string("[cgi_pass] ") + e.what()));
	}
}

void config::add_cgi_param(config::cgi &cgi, const std::vector<Token> &tokens) {
	try {
		check_parameter_count(2, 2, tokens.size());

		std::vector<Token>::const_iterator it = tokens.begin();

		if (it->type != Token::string)
			throw (std::runtime_error("expected string as parameter"));
		std::string	param = it->str;

		it++;
		if(it->type != Token::string)
			throw (std::runtime_error("expected string as value"));
		cgi.params.push_back(std::make_pair(param, it->str));

	} catch (std::exception &e) {
		throw (std::runtime_error(std::string("[cgi_param] ") + e.what()));
	}
}

std::ostream & operator<<(std::ostream & out, const config::cgi & cgi) {
	out << "cgi { pass: " << cgi.pass
		<< ", params: { ";
	for (std::vector<std::pair<std::string, std::string> >::const_iterator it = cgi.params.begin();
		it != cgi.params.end(); it++) {
		if (it != cgi.params.begin())
			out << ", ";
		out << "{ param: " << it->first << ", value: " << it->second << " }";
	}
	out << "}";
	return (out);
}

/* CONFIG :: INDEX ************************************************************/

config::index::index() : is_set(true), path("index.html") {

}

config::index::index(const config::index &other) {
	*this = other;
}

config::index::~index() {

}

config::index &config::index::operator=(const config::index &other) {
	if (this == &other)
		return (*this);
	is_set = other.is_set;
	path = other.path;
	return (*this);
}

void config::add_index(config::index &index, const std::vector<Token> &tokens) {
	try {
		check_parameter_count(1, 1, tokens.size());

		if (tokens[0].type != Token::string)
			throw (std::runtime_error("expected string parameter"));

		index.is_set = true;
		index.path = tokens[0].str;
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[index] ") + e.what()));
	}
}

/* CONFIG :: AUTOINDEX ********************************************************/

config::autoindex::autoindex() : is_set(false) {

}

config::autoindex::autoindex(const config::autoindex &other) {
	*this = other;
}

config::autoindex::~autoindex() {

}

config::autoindex &config::autoindex::operator=(const config::autoindex &other) {
	if (this == &other)
		return (*this);
	is_set = other.is_set;
	return (*this);
}

void config::add_autoindex(config::autoindex &autoindex, const std::vector<Token> &tokens) {
	try {
		check_parameter_count(1, 1, tokens.size());

		if (tokens[0].type != Token::string)
			throw (std::runtime_error("expected string parameter"));

		if (tokens[0].str == "true")
			autoindex.is_set = true;
		else if (tokens[0].str == "false")
			autoindex.is_set = false;
		else
			throw (std::runtime_error("only accepts 'true' and 'false' as parameter"));

	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[autoindex] ") + e.what()));
	}
}

/* CONFIG :: GENERAL **********************************************************/

void config::add_root(std::string & root, const std::vector<Token> & tokens) {
	try {
		check_parameter_count(1, 1, tokens.size());

		if (tokens[0].type != Token::path)
			throw (std::runtime_error("expected path parameter provided"));

		root = tokens[0].str;
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[root] ") + e.what()));
	}
}

void config::add_server_name(std::vector<std::string> & names, const std::vector<Token> & tokens) {
	try {
		check_parameter_count(1, -1, tokens.size());

		for (std::vector<Token>::const_iterator it = tokens.begin();
		it != tokens.end(); it++) {
			if (it->type != Token::string)
				throw (std::runtime_error("non string parameter provided"));
			names.push_back(it->str);
		}
	} catch (std::exception & e) {
		throw (std::runtime_error(std::string("[server_name] ") + e.what()));
	}
}

/* LOCATION *******************************************************************/

Location::Location() : path(""), root("html"), is_prefix(true) {

}

Location::Location(const Location & other) {
	*this = other;
}

Location::Location(const BodyDirective & directive) {
	from_directive(directive);
}

Location::Location(const Server & server) : path(""), root("html"), is_prefix(true) {
	from_server(server);
}

Location::~Location() {
	delete_locations();
}

void Location::delete_locations() {
	for (std::vector<const Location *>::iterator it = locations.begin();
		it != locations.end(); it++) {
		delete *it;
	}
}

Location & Location::operator=(const Location & other) {
	if (this == &other)
		return (*this);
	limit = other.limit;
	path = other.path;
	is_prefix = other.is_prefix;
	root = other.root;
	body = other.body;
	mime = other.mime;
	redirect = other.redirect;
	errorpages = other.errorpages;
	copy_deep_container(locations, other.locations);
	return (*this);
}

void Location::from_directive(const BodyDirective & directive) {
	struct {
		bool	root;
		bool	body_buffer_size;
		bool	body_timeout;
		bool	body_max;
		bool	default_type;
		bool	redirect;
		bool	error_page;
		bool	cgi_pass;
		bool	index;
		bool	autoindex;
	}	was_set;
	was_set.root = false;
	was_set.body_buffer_size = false;
	was_set.body_timeout = false;
	was_set.body_max = false;
	was_set.default_type = false;
	was_set.redirect = false;
	was_set.error_page = false;
	was_set.cgi_pass = false;
	was_set.index = false;
	was_set.autoindex= false;

	/* Here to be in scope during try for proper deletin in case of a throw */
	Location *	loc = NULL;

	/* Try block to catch errors inside of the directives. */
	try {
		std::vector<Token>::const_iterator paramit = directive.parameters.begin();
		if (paramit->type == Token::string && paramit->str == "\\") {
			is_prefix = false;
			++paramit;
		}
		if (paramit->type == Token::path || paramit->type == Token::string) {
			if (is_prefix)
				path += paramit->str;
			else
				path = paramit->str;
			++paramit;
		}
		if (paramit != directive.parameters.end())
			throw (std::runtime_error("too many parameters for directive"));

		for (std::vector<SimpleDirective>::const_iterator it = directive.simple_directives.begin();
		it != directive.simple_directives.end(); it++) {
			if (it->name == "root") {
				if (was_set.root)
					throw (std::runtime_error("multiple root directives"));
				config::add_root(root, it->parameters);
				was_set.root = true;
			} else if (it->name == "client_body_buffer_size") {
				if (was_set.body_buffer_size)
					throw (std::runtime_error("multiple client_body_buffer_size directives"));
				config::add_client_body_buffer_size(body, it->parameters);
				was_set.body_buffer_size = true;
			} else if (it->name == "client_body_timeout") {
				if (was_set.body_timeout)
					throw (std::runtime_error("multiple client_body_timeout directives"));
				config::add_client_body_timeout(body, it->parameters);
				was_set.body_timeout = true;
			} else if (it->name == "client_max_body_size") {
				if (was_set.body_max)
					throw (std::runtime_error("multiple client_max_body_size directives"));
				config::add_client_max_body_size(body, it->parameters);
				was_set.body_max = true;
			} else if (it->name == "default_type") {
				if (was_set.default_type)
					throw (std::runtime_error("multiple default_type directives"));
				config::add_default_type(mime, it->parameters);
				was_set.default_type = true;
			} else if (it->name == "return") {
				if (was_set.redirect)
					throw (std::runtime_error("multiple return directives"));
				config::add_return(redirect, it->parameters);
				was_set.redirect = true;
			} else if (it->name == "error_page") {
				if (!was_set.error_page)
					errorpages.clear();
				config::add_error_page(errorpages, it->parameters);
				was_set.error_page = true;
			} else if (it->name == "index") {
				if (was_set.index)
					throw (std::runtime_error("multiple index directives"));
				config::add_index(index, it->parameters);
				was_set.index = true;
			} else if (it->name == "autoindex") {
				if (was_set.autoindex)
					throw (std::runtime_error("multiple autoindex directives"));
				config::add_autoindex(autoindex, it->parameters);
				was_set.autoindex = true;
			} else if (it->name == "cgi_pass") {
				if (was_set.cgi_pass)
					throw (std::runtime_error("multiple cgi_pass directives"));
				config::add_cgi_pass(cgi, it->parameters);
				was_set.cgi_pass = true;
			} else if (it->name == "cgi_param") {
				config::add_cgi_param(cgi, it->parameters);
			} else if (it->name == "limit_except") {
				config::add_limit_except(limit, it->parameters);
			} else {
				throw (std::runtime_error(std::string("invalid directive: ") + it->name));
			}
		}

		/* simple stack to sttore all future locations and only create them after
		 * all other directives are porcessed */
		std::vector<const BodyDirective *>	location_direc;

		for (std::vector<BodyDirective>::const_iterator it = directive.body_directives.begin();
				it != directive.body_directives.end(); it++) {
			if (it->name == "location") {
				location_direc.push_back(&(*it));
			} else if (it->name == "types") {
				add_types(mime, *it);
			} else {
				throw (std::runtime_error(std::string("invalid directive: ") + it->name));
			}
		}

		/* setting up the locations */
		for (std::vector<const BodyDirective *>::const_iterator it = location_direc.begin();
		it != location_direc.end(); it++) {
			loc = new Location(*this);
			loc->from_directive(**it);
			for (std::vector<const Location *>::iterator locit = locations.begin();
				locit != locations.end(); locit++) {
				if ((*locit)->get_path() == loc->get_path())
					throw (std::runtime_error("two locations with the same path listed"));
			}
			locations.push_back(loc);
			loc = NULL;
		}
	} catch (std::exception & e) {
		delete loc;
		throw (std::runtime_error(std::string("[Location] ") + e.what()));
	}
}

void Location::from_server(const Server & server) {
	root = server.get_root();
	body = server.get_body();
	output = server.get_output();
	mime = server.get_mime();
	errorpages = server.get_errorpages();
}

const Location & Location::get_location(const std::string & uri) const {
	const Location *	longest_prefix = NULL;

	for (std::vector<const Location *>::const_iterator it = locations.begin();
			it != locations.end(); it++) {
		try {
			if ((*it)->is_prefix && uri.compare(0, (*it)->get_path().size(), (*it)->get_path()) == 0 &&
					(longest_prefix == NULL || (*it)->get_path().size() > longest_prefix->get_path().size())) {
				longest_prefix = *it;
			} else if (uri.compare(uri.size() - (*it)->get_path().size(), (*it)->get_path().size(), (*it)->get_path()) == 0) {
				return (*(*it));
			}
		} catch (std::exception & e) { }
	}
	if (longest_prefix == NULL)
		return (*this);
	return (longest_prefix->get_location(uri));
}

#include <iostream>

std::ostream & operator<<(std::ostream & out, const Location & loc) {
	out << "Location { path: " << loc.get_path() <<
		", root: " << loc.get_root() <<
		", " << loc.get_body() <<
		", " << loc.get_output() <<
		", " << loc.get_mime() <<
		", " << loc.get_errorpages() <<
		", " << loc.get_cgi() <<
		" }";
	return (out);
}

/* SERVER *********************************************************************/

Server::Server() {
	root = "html";
}

Server::Server(const Server & other) {
	*this = other;
}

Server::Server(const BodyDirective & directive) {
	from_directive(directive);
}

Server::Server(const Http & http) {
	from_http(http);
}

Server::~Server() {
	delete_locations();
}

void Server::delete_locations() {
	for (std::vector<const Location *>::iterator it = locations.begin();
		it != locations.end(); it++) {
		delete *it;
	}
}

Server & Server::operator=(const Server & other) {
	if (this == & other)
		return (*this);
	root = other.root;
	names = other.names;
	listen = other.listen;
	header = other.header;
	body = other.body;
	output = other.output;
	mime = other.mime;
	errorpages = other.errorpages;
	copy_deep_container(locations, other.locations);
	return (*this);
}

void Server::from_directive(const BodyDirective & directive) {
	struct {
		bool	root;
		bool	header_buffer_size;
		bool	header_timeout;
		bool	large_header_buffers;
		bool	body_buffer_size;
		bool	body_timeout;
		bool	body_max;
		bool	default_type;
		bool	error_page;
		bool	index;
		bool	autoindex;
	}	was_set;
	was_set.root = false;
	was_set.header_buffer_size = false;
	was_set.header_timeout = false;
	was_set.large_header_buffers = false;
	was_set.body_buffer_size = false;
	was_set.body_timeout = false;
	was_set.body_max = false;
	was_set.default_type = false;
	was_set.error_page = false;
	was_set.index = false;
	was_set.autoindex = false;

	/* Here to be in scope during try for proper deletin in case of a throw */
	Location *	loc = NULL;

	/* Try block to catch errors inside of the directives. */
	try {
		for (std::vector<SimpleDirective>::const_iterator it = directive.simple_directives.begin();
		it != directive.simple_directives.end(); it++) {
			if (it->name == "root") {
				if (was_set.root)
					throw (std::runtime_error("multiple root directives"));
				config::add_root(root, it->parameters);
				was_set.root = true;
			} else if (it->name == "client_header_buffer_size") {
				if (was_set.header_buffer_size)
					throw (std::runtime_error("multiple client_header_buffer_size directives"));
				was_set.header_buffer_size = true;
				config::add_client_header_buffer_size(header, it->parameters);
			} else if (it->name == "client_header_timeout") {
				if (was_set.header_timeout)
					throw (std::runtime_error("multiple client_header_timeput directives"));
				config::add_client_header_timeout(header, it->parameters);
				was_set.header_timeout = true;
			} else if (it->name == "large_client_header_buffers") {
				if (was_set.large_header_buffers)
					throw (std::runtime_error("multiple large_client_header_buffers directives"));
				config::add_large_client_header_buffers(header, it->parameters);
				was_set.large_header_buffers = true;
			} else if (it->name == "client_body_buffer_size") {
				if (was_set.body_buffer_size)
					throw (std::runtime_error("multiple client_body_buffer_size directives"));
				config::add_client_body_buffer_size(body, it->parameters);
				was_set.body_buffer_size = true;
			} else if (it->name == "client_body_timeout") {
				if (was_set.body_timeout)
					throw (std::runtime_error("multiple client_body_timeout directives"));
				config::add_client_body_timeout(body, it->parameters);
				was_set.body_timeout = true;
			} else if (it->name == "client_max_body_size") {
				if (was_set.body_max)
					throw (std::runtime_error("multiple client_max_body_size directives"));
				config::add_client_max_body_size(body, it->parameters);
				was_set.body_max = true;
			} else if (it->name == "default_type") {
				if (was_set.default_type)
					throw (std::runtime_error("multiple default_type directives"));
				config::add_default_type(mime, it->parameters);
				was_set.default_type = true;
			} else if (it->name == "error_page") {
				if (!was_set.error_page)
					errorpages.clear();
				config::add_error_page(errorpages, it->parameters);
				was_set.error_page = true;
			} else if (it->name == "index") {
				if (was_set.index)
					throw (std::runtime_error("multiple index directives"));
				config::add_index(index, it->parameters);
				was_set.index = true;
			} else if (it->name == "autoindex") {
				if (was_set.autoindex)
					throw (std::runtime_error("multiple autoindex directives"));
				config::add_autoindex(autoindex, it->parameters);
				was_set.autoindex = true;
			} else if (it->name == "listen") {
				config::add_listen(listen, it->parameters);
			} else if (it->name == "server_name") {
				config::add_server_name(names, it->parameters);
			} else {
				throw (std::runtime_error(std::string("invalid directive: ") + it->name));
			}
		}

		if (listen.size() == 0)
			listen.push_back(config::listen());

		/* simple stack to sttore all future locations and only create them after
		 * all other directives are porcessed */
		std::vector<const BodyDirective *>	location_direc;

		for (std::vector<BodyDirective>::const_iterator it = directive.body_directives.begin();
				it != directive.body_directives.end(); it++) {
			if (it->name == "location") {
				location_direc.push_back(&(*it));
			} else if (it->name == "types") {
				add_types(mime, *it);
			} else {
				throw (std::runtime_error(std::string("invalid directive: ") + it->name));
			}
		}

		/* setting up the locations */
		loc = NULL;
		bool top_level_loc_exists = false;
		for (std::vector<const BodyDirective *>::const_iterator it = location_direc.begin();
		it != location_direc.end(); it++) {
			loc = new Location(*this);
			loc->from_directive(**it);
			if (loc->get_path() == "/")
				top_level_loc_exists = true;
			for (std::vector<const Location *>::iterator locit = locations.begin();
				locit != locations.end(); locit++) {
				if ((*locit)->get_path() == loc->get_path())
					throw (std::runtime_error("two locations with the same path listed"));
			}
			locations.push_back(loc);
			loc = NULL;
		}
		if (top_level_loc_exists == false) {
			/* ensuring that there will always be a valid lcoation with just '/' as
		 	* a path for the Server to find */
			loc = new Location(*this);
			loc->set_path("/");
			locations.push_back(loc);
		}
	} catch (std::exception & e) {
		delete loc;
		throw (std::runtime_error(std::string("[Server] ") + e.what()));
	}
}

void Server::from_http(const Http & http) {
	root = http.get_root();
	header = http.get_header();
	body = http.get_body();
	output = http.get_output();
	mime = http.get_mime();
	errorpages = http.get_errorpages();
}

const Location & Server::get_location(const std::string & uri) const {
	const Location *	longest_prefix = NULL;

	for (std::vector<const Location *>::const_iterator it = locations.begin();
			it != locations.end(); it++) {
		try {
			if ((*it)->is_prefix && uri.compare(0, (*it)->get_path().size(), (*it)->get_path()) == 0 &&
					(longest_prefix == NULL || (*it)->get_path().size() > longest_prefix->get_path().size())) {
				longest_prefix = *it;
			} else if (uri.compare(uri.size() - (*it)->get_path().size(), (*it)->get_path().size(), (*it)->get_path()) == 0) {
				return (*(*it));
			}
		} catch (std::exception &e ) { }
	}
	if (longest_prefix == NULL)
		return (*locations[0]);
	return (longest_prefix->get_location(uri));
}

std::ostream & operator<<(std::ostream & out, const Server & server) {
	out << "Server { listen { ";
	for (std::vector<config::listen>::const_iterator it = server.get_listen().begin();
		it != server.get_listen().end(); it++) {
		if (it != server.get_listen().begin())
			out << ", ";
		out << *it;
	}
	out << " }, server_name { ";
	for (std::vector<std::string>::const_iterator it = server.get_names().begin();
		it != server.get_names().end(); it++) {
		if (it != server.get_names().begin())
			out << ", ";
		out << *it;
	}
	out << " }, root: " << server.get_root() <<
		", header { " << server.get_header() <<
		" }, body { " << server.get_body() <<
		" }, output { " << server.get_output() <<
		" }, mime { " << server.get_mime() <<
		" }, error_page { " << server.get_errorpages() <<
		" } }";
	return (out);
}

/* PORT ***********************************************************************/

Port::Port() {
	listen = config::listen();
	dserver = NULL;
	default_set = false;
}

Port::Port(const Port & other) {
	*this = other;
}

Port::Port(const config::listen & other) {
	listen = config::listen(other);
	dserver = NULL;
	default_set = false;
}

Port::~Port() { }

Port & Port::operator=(const Port & other) {
	if (this == &other)
		return (*this);
	listen = other.listen;
	dserver = other.dserver;
	default_set = other.default_set;
	servername_map = other.servername_map;
	return (*this);
}

void Port::add_server(const Server & server) {
	std::vector<config::listen>::const_iterator it;
	for (it = server.get_listen().begin();
		it != server.get_listen().end(); it++) {
		if (*it == listen)
			break ;
		else if (it->port == listen.port && it->addr == listen.addr)
			throw (std::runtime_error("trying to register the same address:port pair with different backlog values"));
	}
	if (it == server.get_listen().end())
		throw (std::runtime_error("**INTERNAL** server not associated wit this port"));

	if (it->is_default) {
		if (dserver == NULL || default_set == false) {
			default_set = true;
			dserver = &server;
		} else {
			throw (std::runtime_error("trying to set multiple default servers for the same address:port pair"));
		}
	}

	if (servername_map.size() == 0)
		dserver = &server;

	for (std::vector<std::string>::const_iterator nameit = server.get_names().begin();
		nameit != server.get_names().end(); nameit++) {
		if (servername_map.find(*nameit) != servername_map.end())
			throw (std::runtime_error("trying to create to servers with the same name on the same address:port pair"));
		servername_map.insert(std::make_pair(*nameit, &server));
	}
}

const Server & Port::get_server_by_name(const std::string & name) const {
	std::map<std::string, const Server *>::const_iterator server = servername_map.find(name);
	if (server == servername_map.end())
		return (*dserver);
	return (*(server->second));
}

/* HTTP ***********************************************************************/

Http::Http() {
	
}

Http::Http(const Http & other) {
	*this = other;
}

Http::Http(const BodyDirective & directive) {
	from_directive(directive);
}

Http::~Http() {
	delete_servers();
}

Http & Http::operator=(const Http & other) {
	if (this == &other)
		return (*this);
	root = other.root;
	header = other.header;
	body = other.body;
	mime = other.mime;
	errorpages = other.errorpages;
	ports = other.ports;
	copy_deep_container(servers, other.servers);
	return (*this);
}

void Http::delete_servers() {
	for (std::vector<const Server *>::iterator it = servers.begin();
			it != servers.end(); it++) {
		delete *it;
	}
}

void Http::from_directive(const BodyDirective & directive) {
	struct {
		bool	root;
		bool	header_buffer_size;
		bool	header_timeout;
		bool	large_header_buffers;
		bool	body_buffer_size;
		bool	body_timeout;
		bool	body_max;
		bool	default_type;
		bool	index;
		bool	autoindex;
	}	was_set;
	was_set.root = false;
	was_set.header_buffer_size = false;
	was_set.header_timeout = false;
	was_set.large_header_buffers = false;
	was_set.body_buffer_size = false;
	was_set.body_timeout = false;
	was_set.body_max = false;
	was_set.default_type = false;
	was_set.index = false;
	was_set.autoindex = false;

	/* Here to be in scope during the catch statement for deleting in case of a throw */
	Server *	server = NULL;

	/* Try block to catch errors inside of the directives. */
	try {
		for (std::vector<SimpleDirective>::const_iterator it = directive.simple_directives.begin();
			it != directive.simple_directives.end(); it++) {
			if (it->name == "root") {
				if (was_set.root)
					throw (std::runtime_error("multiple root directives"));
				config::add_root(root, it->parameters);
				was_set.root = true;
			} else if (it->name == "client_header_buffer_size") {
				if (was_set.header_buffer_size)
					throw (std::runtime_error("multiple client_header_buffer_size directives"));
				config::add_client_header_buffer_size(header, it->parameters);
				was_set.header_buffer_size = true;
			} else if (it->name == "client_header_timeout") {
				if (was_set.header_timeout)
					throw (std::runtime_error("multiple client_header_timeput directives"));
				config::add_client_header_timeout(header, it->parameters);
				was_set.header_timeout = true;
			} else if (it->name == "large_client_header_buffers") {
				if (was_set.large_header_buffers)
					throw (std::runtime_error("multiple large_client_header_buffers directives"));
				config::add_large_client_header_buffers(header, it->parameters);
				was_set.large_header_buffers = true;
			} else if (it->name == "client_body_buffer_size") {
				if (was_set.body_buffer_size)
					throw (std::runtime_error("multiple client_body_buffer_size directives"));
				config::add_client_body_buffer_size(body, it->parameters);
				was_set.body_buffer_size = true;
			} else if (it->name == "client_body_timeout") {
				if (was_set.body_timeout)
					throw (std::runtime_error("multiple client_body_timeout directives"));
				config::add_client_body_timeout(body, it->parameters);
				was_set.body_timeout = true;
			} else if (it->name == "client_max_body_size") {
				if (was_set.body_max)
					throw (std::runtime_error("multiple client_max_body_size directives"));
				config::add_client_max_body_size(body, it->parameters);
				was_set.body_max = true;
			} else if (it->name == "default_type") {
				if (was_set.default_type)
					throw (std::runtime_error("multiple default_type directives"));
				config::add_default_type(mime, it->parameters);
				was_set.default_type = true;
			} else if (it->name == "index") {
				if (was_set.index)
					throw (std::runtime_error("multiple index directives"));
				config::add_index(index, it->parameters);
				was_set.index = true;
			} else if (it->name == "autoindex") {
				if (was_set.autoindex)
					throw (std::runtime_error("multiple autoindex directives"));
				config::add_autoindex(autoindex, it->parameters);
				was_set.autoindex = true;
			} else if (it->name == "error_page") {
				config::add_error_page(errorpages, it->parameters);
			} else {
				throw (std::runtime_error(std::string("invalid directive: ") + it->name));
			}
		}

		/* simple stack to store all future servers and only create them after
		 * all other directives are processed */
		std::vector<const BodyDirective *>	server_direc;

		for (std::vector<BodyDirective>::const_iterator it = directive.body_directives.begin();
			it != directive.body_directives.end(); it++) {
			if (it->name == "server") {
				server_direc.push_back(&(*it));
			} else if (it->name == "types") {
				add_types(mime, *it);
			} else {
				throw (std::runtime_error(std::string("invalid directive: ") + it->name));
			}
		}
		
		/* setting up the server */
		if (server_direc.size() == 0)
			servers.push_back(new Server(*this));
		else {
			for (std::vector<const BodyDirective *>::const_iterator it = server_direc.begin();
					it != server_direc.end(); it++) {
				server = new Server(*this);
				server->from_directive(**it);
				servers.push_back(server);
				server = NULL;
				/* using server.back() should cause future references to this
				 * server to be focused on this instance making the code for
				 * Ports safe since they will always reffer to the server in
				 * their Http class */
				const Server & server_ref = *(servers.back());
				for (std::vector<config::listen>::const_iterator listenit = server_ref.get_listen().begin();
					listenit != server_ref.get_listen().end(); listenit++) {
					std::map<struct sockaddr_in, Port>::iterator portit = ports.find(listenit->get_sockaddr());
					if (portit == ports.end()) {
						Port port(*listenit);
						port.add_server(server_ref);
						ports.insert(std::make_pair(listenit->get_sockaddr(), port));
					} else {
						portit->second.add_server(server_ref);
					}
				}
			}
		}
	} catch (std::exception & e) {
		delete server;
		throw (std::runtime_error(std::string("[Http] ") + e.what()));
	}
}

const Server & Http::get_default_server(const struct sockaddr_in & sockaddr) const {
	std::map<struct sockaddr_in, Port>::const_iterator port = ports.find(sockaddr);
	if (port == ports.end())
		throw (std::runtime_error("No corresponding port found"));
	return (port->second.get_dserver());
}

const Server & Http::get_server(const struct sockaddr_in & sockaddr, const std::string & name) const {
	std::map<struct sockaddr_in, Port>::const_iterator port = ports.find(sockaddr);
	if (port == ports.end())
		throw (std::runtime_error("No corresponding port found"));
	return (port->second.get_server_by_name(name));
}

std::ostream & operator<<(std::ostream & out, const Http & http) {
	out << "Http { root: " << http.get_root() <<
		", header { " << http.get_header() <<
		" }, body { " << http.get_body() <<
		" }, output { " << http.get_output() <<
		" }, mime { " << http.get_mime() <<
		" }, error_page { " << http.get_errorpages() <<
		" } }";
	return (out);
}

bool operator<(const struct sockaddr_in & lhs, const struct sockaddr_in & rhs) {
	if (lhs.sin_family < rhs.sin_family)
		return (true);
	else if (lhs.sin_family != rhs.sin_family)
		return (false);
	if (lhs.sin_port < rhs.sin_port)
		return (true);
	else if (lhs.sin_port != rhs.sin_port)
		return (false);
	if (lhs.sin_addr.s_addr < rhs.sin_addr.s_addr)
		return (true);
	return (false);
}

/* UTILS **********************************************************************/

/**	@brief Checks if size is outside of the min and max. If so it prints the
 * appropriate response inserting the string directive into the error message.
 * If -1 is provided as a value for min or max the value will not be checked.
*/
void config::check_parameter_count(int min, int max, int size) {
	if (min != -1 && size < min)
		throw (std::runtime_error("too few parameter provided"));
	if (max != -1 && size > max)
		throw (std::runtime_error("too many parameter provided"));
}
