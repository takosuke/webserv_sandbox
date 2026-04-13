#pragma once
#include <iostream>

class	HttpParser {
	public:
		HttpParser() : _i(0) {}
		int _i; // dummy counter 
		// feed function should complain if more data than declared was sent
		// (400 bad request)
		void feed(const char *data, int len){
			_i++;
			(void)data;
			(void)len;
		}
		bool complete() const {
			std::cout << "complete?" << _i <<  std::endl;
			if (_i == 2)
				return true;
			return false;
		}
		bool error() const;
};
