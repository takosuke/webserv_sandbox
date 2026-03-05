CFLAGS = -Wall -Wextra -Werror -std=c++98
CC = c++

TARGET = webserv
SOURCE = webserv_poll.cpp

.PHONY: all
all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $< -o $@

.PHONY: clean
clean:
	rm -f $(TARGET)

.PHONY: fclean
fclean: clean

.PHONY: re
re: clean all

