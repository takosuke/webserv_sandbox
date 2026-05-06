#pragma once

#include <iostream>
#include <sstream>
#include <string>

enum LogLevel {
	LOG_DEBUG = 0,
	LOG_INFO = 1,
	LOG_WARN = 2,
	LOG_ERROR = 3,
};

class Logger {
	public:
		static Logger& instance() {
			static Logger inst;
			return inst;
		}

		void setLevel(LogLevel level) { _level = level; }

		std::ostream& log(LogLevel level, const std::string& component) {
			if (level < _level)
				return _null; // discard
			std::cerr << levelTag(level) << " [" << component << "] ";
			return std::cerr;
		}
	
	private:
		Logger() : _level(LOG_DEBUG), _null(0) {}

		LogLevel	_level;
		std::ostream _null; // sink that discards everything

		std::string levelTag(LogLevel level) {
			switch (level) {
				case LOG_DEBUG: return "\033[2m[DBG]";   // dim
				case LOG_INFO:  return "\033[34m[INF]";  // blue
				case LOG_WARN:  return "\033[33m[WRN]";  // yellow
				case LOG_ERROR: return "\033[31m[ERR]";  // red
				default:        return "[???]";
			}
		}
};
//
// Convenience macros
#define LOG_DEBUG(component) Logger::instance().log(LOG_DEBUG, component) << "\033[0m"
#define LOG_INFO(component)  Logger::instance().log(LOG_INFO,  component) << "\033[0m"
#define LOG_WARN(component)  Logger::instance().log(LOG_WARN,  component) << "\033[0m"
#define LOG_ERROR(component) Logger::instance().log(LOG_ERROR, component) << "\033[0m"

