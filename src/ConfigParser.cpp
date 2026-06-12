#include "ConfigParser.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <stack>

/* TOKEN **********************************************************************/

Token::Token() {
	line = -1;
	type = invalid;
	str = "";
	num = -1;
}

Token::Token(const std::string & s) {
	line = -1;
	type = invalid;
	str = s;
	num = -1;
}

Token::Token(const Token & other) {
	*this = other;
}

Token::~Token() { }

Token & Token::operator=(const Token & other) {
	if (this == &other)
		return (*this);
	line = other.line;
	type = other.type;
	str = other.str;
	num = other.num;
	return (*this);
}

const char *tokentypestring[10] = {
	"semicolon",
	"lcurly",
	"rcurly",
	"path",
	"url",
	"string",
	"number",
	"time",
	"memory",
	"invalid"
};

std::ostream & operator<<(std::ostream & out, const Token & token) {
	out << "type: " << tokentypestring[token.type] <<
		", str: \"" << token.str << '"';
	if (token.type > Token::string && token.type < Token::invalid)
		out << ", num: " << token.num;
	out << ", line: " << token.line;
	return (out);
}
 
/* DIRECTIVE ******************************************************************/

Directive::Directive(std::vector<Token> & tokens) {
	std::vector<Token>::iterator	it = tokens.begin();

	name = it->str;
	++it;
	parameters.insert(parameters.begin(), it, tokens.end());
}

Directive & Directive::operator=(const Directive & other) {
	if (this == &other)
		return (*this);
	this->name = other.name;
	this->parameters = other.parameters;
	return (*this);
}

SimpleDirective & SimpleDirective::operator=(const SimpleDirective & other) {
	if (this == &other)
		return (*this);
	this->name = other.name;
	this->parameters = other.parameters;
	return (*this);
}

std::ostream & operator<<(std::ostream & out, const SimpleDirective & sd) {
	out << sd.name << ":";
	for (std::vector<Token>::const_iterator it = sd.parameters.begin();
		it < sd.parameters.end(); ++it) {
		out << " " << *it;
	}
	out << ";";
	return (out);
}

BodyDirective & BodyDirective::operator=(const BodyDirective & other) {
	if (this == &other)
		return (*this);
	this->name = other.name;
	this->parameters = other.parameters;
	this->simple_directives = other.simple_directives;
	this->body_directives = other.body_directives;
	return (*this);
}

void BodyDirective::add_directive(SimpleDirective directive) {
	simple_directives.push_back(directive);
}

void BodyDirective::add_directive(BodyDirective directive) {
	body_directives.push_back(directive);
}

std::ostream & operator<<(std::ostream & out, const BodyDirective & bd) {
	out << bd.name << ":";
	for (std::vector<Token>::const_iterator it = bd.parameters.begin();
		it < bd.parameters.end(); it++) {
		out << " " << *it;
	}
	out << " {";
	for (std::vector<SimpleDirective>::const_iterator it = bd.simple_directives.begin();
		it < bd.simple_directives.end(); it++) {
		out << " " << *it;
	}
	for (std::vector<BodyDirective>::const_iterator it = bd.body_directives.begin();
		it < bd.body_directives.end(); it++) {
		out << " " << *it;
	}
	out << " }";
	return (out);
}

MainDirective::MainDirective() {
	name = "main";
}

MainDirective::~MainDirective() { }

/* LEXER **********************************************************************/

bool operator<(const std::pair<int, TokenLex> & a, const std::pair<int, TokenLex> & b) {
	return (a.first < b.first);
}

Lexer::Lexer() { }

Lexer::Lexer(const Lexer & other) {
	*this = other;
}

Lexer::Lexer(std::string const & filename) {
	load(filename);
}

Lexer & Lexer::operator=(const Lexer & other) {
	if (this == &other)
		return (*this);
	lexset = other.lexset;
	line = "";
	nline = -1;
	return (*this);
}

Lexer::~Lexer() {
	if (stream.is_open())
		stream.close();
	for (std::multiset<std::pair<int, TokenLex *> >::iterator it = lexset.begin();
			it != lexset.end(); ++it) {
		delete it->second;
	}
}

void Lexer::getline() {
	std::getline(stream, line);
	lineit = line.begin();
	++nline;
}

void Lexer::load(std::string const & filename) {
	if (stream.is_open()) {
		std::cout << "[Lexer] Closing file: " << this->filename << std::endl;
		stream.close();
	}
	this->filename = filename;
	std::cout << "[Lexer] Opening file: " << this->filename << std::endl;
	stream.open(filename.c_str(), std::ios_base::in);
	nline = 0;
	getline();
}

bool Lexer::add_lex(int precedence, TokenLex * lex) {
	if (lex == NULL) {
		std::cerr << "[Lexer] failed to add TokenLex" << std::endl;
		return (false);
	}
	lexset.insert(std::pair<int, TokenLex *>(precedence, lex));
	return (true);
}

bool Lexer::lex_file() {
	while (!stream.eof()) {
		while (std::isspace(*lineit))
			++lineit;
		while (lineit == line.end()) {
			getline();
			while (std::isspace(*lineit))
				++lineit;
			if (*lineit == '\0' && stream.eof())
				return (true);
		}

		if (*lineit == '#') {
			lineit = line.end();
			continue;
		}

		Token token = lex();
		if (token.type == Token::invalid) {
			return (false);
		} else {
			tokens.push_back(token);
		}
	}
	return (true);
}

Token Lexer::lex() {
	for (std::multiset<std::pair<int, TokenLex *> >::iterator it = lexset.begin();
			it != lexset.end(); ++it) {
		if (it->second->check(lineit)) {
			Token token = it->second->construct();

			token.line = nline;
			return (token);
		}
	}
	return (Token());
}

/* GROUPER ********************************************************************/

Grouper::Grouper() {};

Grouper::Grouper(std::string const & filename) {
	load(filename);
}

Grouper::Grouper(const Grouper & other) {
	*this = other;
}

Grouper::~Grouper() {}

Grouper & Grouper::operator=(const Grouper & other) {
	if (this == &other)
		return (*this);
	lexer = other.lexer;
	main = other.main;
	return (*this);
}

void Grouper::load(std::string const & filename) {
	lexer.load(filename);

	lexer.add_lex(0, new SemicolonLex());
	lexer.add_lex(1, new LCurlyLex());
	lexer.add_lex(1, new RCurlyLex());
	lexer.add_lex(10, new NumberLex());
	lexer.add_lex(20, new PathLex());
	lexer.add_lex(20, new UrlLex());
	lexer.add_lex(1000, new StringLex());
	lexer.lex_file();
}

bool Grouper::group() {
	std::stack<BodyDirective>	stack;
	std::vector<Token>			tokenline;

	stack.push(MainDirective());

	std::vector<Token>::const_iterator	it = lexer.tokens.begin();
	while (it != lexer.tokens.end()) {
		if (it->type == Token::semicolon) {
			if (tokenline.size() <= 1) {
				std::cerr << "[Grouper] Trying to create a simple-directive with only a name (" << *it << ")" << std::endl;
			} else if (tokenline.front().type != Token::string) {
				std::cerr << "[Grouper] Trying to create a simple-directive with a non-string token as name (" << tokenline.front() << ")" << std::endl;
			} else {
				stack.top().add_directive(SimpleDirective(tokenline));
			}
			tokenline.resize(0);
			++it;
		} else if (it->type == Token::lcurly) {
			if (tokenline.front().type != Token::string) {
				std::cerr << "[Grouper] Trying to create a body-directive with a non-string token as name (" << tokenline.front() << ")" << std::endl;
			} else {
				stack.push(BodyDirective(tokenline));
			}
			tokenline.resize(0);
			++it;
		} else if (it->type == Token::rcurly) {
			if (stack.size() <= 1) {
				std::cerr << "[Grouper] Trying to close a body-directive that was not opened. Check if there are too many closing curly-bracketes ('}') or if you don't have a curly-bracket ('{') after 'http', 'server', or 'location'" << std::endl;
				return (false);
			}
			if (tokenline.size() != 0) {
				std::cerr << "[Grouper] Trying to close body directive before closing contained directive" << std::endl;
				return (false);
			}
			BodyDirective new_directive = stack.top();

			stack.pop();
			stack.top().add_directive(new_directive);
			++it;
		} else {
			tokenline.push_back(*it++);
		}
	}
	bool ret = true;
	if (tokenline.size() != 0) {
		ret = false;
		std::cerr << "[Grouper] remaining tokens at the end of the file. Terminate with a semicolon (';')" << std::endl;
	} else if (stack.size() > 1) {
		ret = false;
		std::cerr << "[Grouper] unclosed body-directives. Close them with curly brackets ('}')" << std::endl;
	}
	main = stack.top();
	return (ret);
}

/* SPECIFIC LEXER PATTERNS ****************************************************/

TokenLex::TokenLex() { }
TokenLex::~TokenLex() { }

SemicolonLex::SemicolonLex() { }
SemicolonLex::~SemicolonLex() { }

LCurlyLex::LCurlyLex() { }
LCurlyLex::~LCurlyLex() { }

RCurlyLex::RCurlyLex() { }
RCurlyLex::~RCurlyLex() { }

PathLex::PathLex() { }
PathLex::~PathLex() { }

NumberLex::NumberLex() { }
NumberLex::~NumberLex() { }

UrlLex::UrlLex() { };
UrlLex::~UrlLex() { };

StringLex::StringLex() { }
StringLex::~StringLex() { }

bool SemicolonLex::check(std::string::iterator & it) {
	if (*it == ';') {
		++it;
		return (true);
	}
	return (false);
}

Token SemicolonLex::construct() const {
	Token token = Token(std::string(start, end));

	token.type = Token::semicolon;
	return (token);
}

bool LCurlyLex::check(std::string::iterator & it) {
	start = it;
	if (*it == '{') {
		++it;
		end = it;
		return (true);
	}
	return (false);
}

Token LCurlyLex::construct() const {
	Token token = Token(std::string(start, end));

	token.type = Token::lcurly;
	return (token);
}

bool RCurlyLex::check(std::string::iterator & it) {
	start = it;
	if (*it == '}') {
		++it;
		end = it;
		return (true);
	}
	return (false);
}

Token RCurlyLex::construct() const {
	Token token = Token(std::string(start, end));

	token.type = Token::rcurly;
	return (token);
}

bool PathLex::check(std::string::iterator & it) {
	start = it;
	if (*it != '/')
		return (false);
	++it;
	while (std::isprint(*it) && !std::isspace(*it) && *it != ';') {
		++it;
	}
	end = it;
	return (true);
}

Token PathLex::construct() const {
	Token token = Token(std::string(start, end));

	token.type = Token::path;
	return (token);
}

bool NumberLex::check(std::string::iterator & it) {
	start = it;
	if (!std::isdigit(*it))
		return (false);
	while (std::isdigit(*it))
		++it;
	if (*it == 'k' || *it == 'm' ||
		*it == 's' || *it == 'h')
		++it;
	end = it;
	if (std::isspace(*it) || *it == ';')
		return (true);
	it = start;
	return (false);
}

Token NumberLex::construct() const {
	Token token = Token(std::string(start, end));

	token.num = std::strtoul(&*start, NULL, 10);
	switch (*(end - 1)) {
		case 'k':
			token.type = Token::memory;
			token.num *= 1000;
			break ;
		case 'm':
			token.type = Token::memory;
			token.num *= 1000000;
			break ;
		case 's':
			token.type = Token::time;
			break ;
		case 'h':
			token.type = Token::time;
			token.num *= 3600;
			break ;
		default:
			token.type = Token::number;
	}
	return (token);
}

static bool cmp_str_it(const std::string & str, std::string::iterator & it);

/** Checks a bunch of strings for different schemes against the strign starting
 * at iterator it.
 * Returns false if none of them match or if the ':' delimiter after the scheme
 * is missing.
*/
bool UrlLex::check(std::string::iterator &it) {
	const static std::string schemes[15] = {
		"blob", "data", "file", "ftp", "http", "https", "javascript",
		"mailto", "resource", "ssh", "tel", "urn", "view-source", "ws", "wss"
	};

	start = it;

	int i;
	for (i = 0; i < 15; i++) {
		if (cmp_str_it(schemes[i], it) && *it == ':') {
			break ;
		}
		it = start;
	}
	if (i == 15)
		return (false);
	while (!std::isspace(*it) && *it != '\0' && *it != ';')
		++it;
	end = it;
	return (true);
}

static bool cmp_str_it(const std::string & str, std::string::iterator & it) {
	std::string::iterator start = it;

	for (unsigned long i = 0; i < str.size(); i++) {
		if (str[i] != *it) {
			it = start;
			return (false);
		}
		++it;
	}
	return (true);
}

Token UrlLex::construct() const {
	Token token = Token(std::string(start, end));

	token.type = Token::url;
	return (token);
}

bool StringLex::check(std::string::iterator & it) {
	start = it;
	if (!std::isprint(*it))
		return (false);
	while (std::isprint(*it) && !std::isspace(*it) && *it != ';') {
		++it;
	}
	end = it;
	return (true);
}

Token StringLex::construct() const {
	Token token = Token(std::string(start, end));

	token.type = Token::string;
	return (token);
}
