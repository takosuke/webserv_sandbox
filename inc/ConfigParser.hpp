#pragma once

#include <fstream>
#include <set>
#include <vector>
#include <string>
#include <iostream>

/* TOKEN **********************************************************************/

/**	Struct that holds information about a token identified by one of the
 * TokenLex classes.
 * A Token is invalid by default and needs to be set to the specific type by the
 * respective TokenLex that created it through it's construct() function.
 *
 * For debugging and better identification of mistakes in the config file the
 * Token have a line integer variable. Since the TokenLexers operate on
 * std::string::iterators this value needs to be set by the Lexer that is
 * responsible for reading the file line by line and therefore also knows the
 * line position the file is in.
 */
struct Token {
public:
	int					line;
	enum {
		semicolon,
		lcurly,
		rcurly,
		path,
		url,
		string,
		number,
		time,
		memory,
		invalid
	}					type;
	std::string			str;
	long unsigned int	num;

	Token();
	Token(const std::string & s);
	Token(const Token & other);
	~Token();

	Token & operator=(const Token & other);
};

std::ostream & operator<<(std::ostream & out, const Token & token);

/* DIRECTIVE ******************************************************************/

struct Directive {
public:
	std::string			name;
	std::vector<Token>	parameters;

	Directive() {};
	Directive(std::vector<Token> & tokens);
	~Directive() {};

	Directive & operator=(const Directive & other);
};

struct SimpleDirective : public Directive {
public:
	SimpleDirective() {};
	SimpleDirective(std::vector<Token> & tokens) : Directive(tokens) {};
	~SimpleDirective() {};

	SimpleDirective & operator=(const SimpleDirective & other);
};

std::ostream & operator<<(std::ostream & out, const SimpleDirective & bd);

struct BodyDirective : public Directive {
public:
	std::vector<SimpleDirective>	simple_directives;	
	std::vector<BodyDirective>		body_directives;	

	BodyDirective() {};
	BodyDirective(std::vector<Token> & tokens) : Directive(tokens) {};
	~BodyDirective() {};

	BodyDirective & operator=(const BodyDirective & other);

	void add_directive(SimpleDirective directive);
	void add_directive(BodyDirective directive);
};

std::ostream & operator<<(std::ostream & out, const BodyDirective & bd);

struct MainDirective : public BodyDirective {
public:
	MainDirective();
	~MainDirective();
};

/* LEXER **********************************************************************/

/*	Basic Functor class:
 *	Use this to create derived classes that contain an overload for the
 *	'operator()' that contains the logic to identify and create a token.
 */
class TokenLex {
protected:
	std::string::iterator	start, end;

public:
	TokenLex();
	virtual ~TokenLex();

	virtual bool check(std::string::iterator &) { return (false); };
	virtual Token construct() const { return (Token()); };
};

bool operator<(const std::pair<int, TokenLex> & a, const std::pair<int, TokenLex> & b);

class Lexer {
private:
	std::multiset<std::pair<int, TokenLex *> > lexset;
	std::string				filename;
	std::ifstream			stream;
	std::string				line;
	std::string::iterator	lineit;
	int						nline;

	void	getline();

public:
	Lexer();
	Lexer(const Lexer & other);
	Lexer(std::string const & filename);
	~Lexer();

	Lexer & operator=(const Lexer & other);

	std::vector<Token>			tokens;

	void	load(std::string const & filename);
	bool	add_lex(int precedence, TokenLex * lex);
	bool	lex_file();
	Token	lex();
};

/* GROUPER ********************************************************************/

class Grouper {
private:
	Lexer	lexer;

public:
	BodyDirective	main;

	Grouper();
	Grouper(std::string const & filename);
	Grouper(const Grouper & other);
	~Grouper();

	Grouper & operator=(const Grouper & other);

	void	load(std::string const & filename);
	bool	group();
};

/* SPECIFIC LEXER PATTERNS ****************************************************/

class SemicolonLex : public TokenLex {
public:
	SemicolonLex();
	~SemicolonLex();
	bool check(std::string::iterator & it);
	Token construct() const;
};

class LCurlyLex : public TokenLex {
public:
	LCurlyLex();
	~LCurlyLex();
	bool check(std::string::iterator & it);
	Token construct() const;
};

class RCurlyLex : public TokenLex {
public:
	RCurlyLex();
	~RCurlyLex();
	bool check(std::string::iterator & it);
	Token construct() const;
};

class PathLex : public TokenLex {
public:
	PathLex();
	~PathLex();
	bool check(std::string::iterator & it);
	Token construct() const;
};

class NumberLex : public TokenLex {
public:
	NumberLex();
	~NumberLex();
	bool check(std::string::iterator & it);
	Token construct() const;
};

class UrlLex : public TokenLex {
public:
	UrlLex();
	~UrlLex();
	bool check(std::string::iterator & it);
	Token construct() const;
};

class StringLex : public TokenLex {
public:
	StringLex();
	~StringLex();
	bool check(std::string::iterator & it);
	Token construct() const;
};
