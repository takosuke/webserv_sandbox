#include "autoindex.hpp"

#include <cstring>

#include <iostream>
#include <iomanip>

static void info();

int main(int ac, char * av[]) {
	if (ac == 1)
		return (info(), 0);

	autoindex::Flags	flags;

	int i;
	for (i = 1; i < ac; i++) {
		if (av[i][0] != '-')
			break ;
		if (!std::strncmp(av[i], "--exact-size=", 13)) {
			if (!std::strncmp(av[i] + 13, "on", 3))
				flags.exact_size = true;
			else if (!std::strncmp(av[i] + 13, "off", 4))
				flags.exact_size = false;
			else
				std::cerr << "invalid option '" << av[i] + 13 << "' for option --exact-size" << std::endl;
		} else if (!std::strncmp(av[i], "--format=", 9)) {
			if (!std::strncmp(av[i] + 9, "html", 5))
				flags.format = autoindex::html;
			else if (!std::strncmp(av[i] + 9, "json", 5))
				flags.format = autoindex::json;
			else if (!std::strncmp(av[i] + 9, "xml", 4))
				flags.format = autoindex::xml;
			else
				std::cerr << "invalid option '" << av[i] + 9 << "' for option --format" << std::endl;
		}
	}
	if (ac - 2 < i)
		return (-1);
	std::string	dirname(av[i]);
	std::string	dirpath(av[i + 1]);
	if (dirpath.size() == 0)
		dirpath = "./";
	else if (dirpath[dirpath.size() - 1])
		dirpath.insert(dirpath.size(), 1, '/');
	std::cout << autoindex::as_html(dirname, dirpath, flags);
	return (0);
}

static void info() {
	std::cout << "Usage: " << "autoindex" << "[OPTION] ... DIRECTORYNAME DIRECTORYPATH" << std::endl;
	std::cout << "Lists information about the files in DIRECTORY sorted alphabetically" << std::endl;
	std::cout << "Options:" << std::endl;
	std::cout << "--exact-size=[on | off]" << std::endl <<
		"if 'on' uses the exact size of files in bytes. if 'off' rounds the size to kilobytes, megabytes, or gigabytes." << std::endl <<
		"Default: 'on'" << std::endl;
	std::cout << "--format=[html | xml | json]" << std::endl <<
		"determines the output format." << std::endl <<
		"Default: ’html'" << std::endl;
}
