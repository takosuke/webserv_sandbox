#pragma once

#include <dirent.h>
#include <sys/stat.h>
#include <ctime>

#include <string>
#include <sstream>

namespace autoindex {
	enum e_fmt {
		html,
		xml,
		json
	};

	struct Flags {
	public:
		bool	exact_size;
		e_fmt	format;
	};

	class File {
		private:
			std::string	_name;
			bool		_hidden;
			std::tm		_mtime;
			mode_t		_type;
			off_t		_size;

			int			_err;

		public:
			File();
			File(const std::string & dirpath, struct dirent & dirent);
			~File();

			std::string	name() const;
			std::tm		mtime() const;
			mode_t		type() const;
			off_t		size() const;

			bool		is_socket() const;
			bool		is_symlink() const;
			bool		is_regular() const;
			bool		is_block_device() const;
			bool		is_directory() const;
			bool		is_character_device() const;
			bool		is_pipe() const;

			bool		is_self() const;
			bool		hidden() const;

			bool	fail() const;
			int		err() const;
	};

	std::string as_html(const std::string & locationname,
		const std::string & dirpath, const Flags & flags);
	std::string as_xml(const std::string & locationname,
		const std::string & dirpath, const Flags & flags);

	std::string	timestr(const std::tm & tm);
	std::string	exactsizestr(off_t size);
	std::string	roundedsizestr(off_t size);
	std::string cropfilename(const std::string & filename, int width,
		std::string ellipses);
}

