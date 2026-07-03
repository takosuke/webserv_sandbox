#include "autoindex.hpp"

#include <sys/types.h>
#include <dirent.h>
#include <cerrno>

#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

std::string autoindex::as_html(const std::string & locationname,
	const std::string & dirpath, const Flags & flags) {
	DIR				* dir;
	struct dirent	* dirent;

	dir = opendir(dirpath.c_str());
	if (dir == NULL)
		return ("Couldn't open directory: " + locationname);

	std::vector<File>	files;

	errno = 0;
	dirent = readdir(dir);
	while (errno == 0 && dirent != NULL) {
		files.push_back(File(dirpath, *dirent));
		errno = 0;
		dirent = readdir(dir);
	}
	closedir(dir);

	std::ostringstream	response;

	response << "<html>" << std::endl <<
		"<head><title>Index of " << locationname << "</title></head>" <<
		"<body>" << std::endl <<
		"<h1>Index of " << locationname << "</h1><hr><pre>" << std::endl;
	for (std::vector<File>::const_iterator it = files.begin();
	it != files.end(); it++) {
		std::string filename = it->name();
		if (it->hidden() || it->is_self())
			continue ;
		response << "<a href=" << locationname << filename << ">";
		if (filename.size() > 50) {
			response << cropfilename(filename, 50, "..>");
		} else {
			response << filename;
		}
		response << "</a>";
		if (it->err() == 0) {
			if (filename.size() < 50) {
				response << std::string(50 - filename.size(), ' ');
			}

			response << timestr(it->mtime());

			std::string sizestr;
			if (!it->is_regular() || !it->is_symlink()) {
				sizestr = "-";
			} else {
				if (flags.exact_size) {
					sizestr = exactsizestr(it->size());
				} else {
					sizestr = roundedsizestr(it->size());
				}
			}
			response << std::setw(15) << sizestr << std::endl;;
		} else {
			response << std::setw(65) << "Error reading file data" << std::endl;
		}
	}
	response << "</pre><hr></body>" << std::endl <<
		"</html>" << std::endl;
	return (response.str());
}

// static std::string	fmt_xml(const std::vector<File> & files) {
// 	std::string	response;
//
// 	response << "<?xml version=\"1.0\"?>" << std::endl <<
// 		<< "<list>" << std::endl;
// 	for (std::vector<autoindex::File>::const_iterator it = files.begin();
// 			it != files.end(); it++) {
// 		response << it->fmt_xml();
// 	}
// 	response << "</list>" << std::endl;
// 	return (response);
// }
//
// static std::string	fmt_json(const std::vector<File> & files) {
// 	std::string	response;
//
// 	response << "<?xml version=\"1.0\"?>" << std::endl <<
// 		<< "<list>" << std::endl;
// 	for (std::vector<autoindex::File>::const_iterator it = files.begin();
// 			it != files.end(); it++) {
// 		response << it->fmt_xml();
// 	}
// 	response << "</list>" << std::endl;
// 	return (response);
// }

/* Format: "dd-Mmm-yyy hh:mm"
 *
 * MMM is a three letter combination identifying the month.
 */
std::string autoindex::timestr(const struct std::tm & tm) {
	static const char	monthstr[12][4] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "sep", "Oct", "Nov", "Dec"
	};

	std::ostringstream stream;
	stream << std::setfill('0') << std::setw(2) << tm.tm_mday <<
		"-" << monthstr[tm.tm_mon] <<
		"-" << std::setw(4) << 1900 + tm.tm_year <<
		" " << std::setw(2) << tm.tm_hour <<
		":" << std::setw(2) << tm.tm_min;
	return (stream.str());
}

std::string autoindex::exactsizestr(off_t size) {
	std::ostringstream out;

	out << size;
	return (out.str());
}

std::string autoindex::roundedsizestr(off_t size) {
	std::stringstream	out;
	char				sizeindicator[4] = "KMG";
	int					i = 0;

	size /= 1000;
	while (i < 2) {
		if (size < 1000)
			break ;
		++i;
		size /= 1000;
	}
	out << size << sizeindicator[i];
	return (out.str());
}

std::string autoindex::cropfilename(const std::string & filename, int width,
	std::string ellipses) {
	return (filename.substr(0, width - ellipses.size()) + ellipses);
}
