# **************************************************************************** #

NAME	:= webserv

# * DIRECTORIES ************************************************************** #

SDIR	:= src/
IDIR	:= inc/
ODIR	:= obj/

# * FILES ******************************************************************** #

SRCS	:=
vpath %.cpp $(SDIR)
MAIN	?= webserv.cpp
SRCS	+= $(MAIN)

SRCS	+= ConfigParser.cpp
SRCS	+= Config.cpp

SRCS	+= ServerConnection.cpp
SRCS	+= ClientConnection.cpp

SRCS	+= EpollLoop.cpp

SRCS	+= ServerBlock.cpp

SRCS	+= Request.cpp
SRCS	+= Response.cpp
SRCS	+= ScratchBuffer.cpp

SRCS	+= utils.cpp

SRCS	+= autoindex.cpp
SRCS	+= autoindex_File.cpp

OBJS	:= $(SRCS:.cpp=.o)
OBJS	:= $(addprefix $(ODIR), $(OBJS))

DEPS	:= $(OBJS:.o=.d)

# * COMMANDS ***************************************************************** #

CXX		:= c++
RM		:= rm -rf
MKDIR	:= mkdir -p

# * FLAGS ******************************************************************** #

CXXFLAGS	?=
CXXFLAGS	+= -Wall -Werror -Wextra
CXXFLAGS	+= -std=c++98 -MMD -MP

ifeq ($(ls -al $(which cc) | sed -e 's/.*-> //' | sed -e 's/\(.*\/\)*//'), "clang")
	CXXFLAGS	+= -Wno-type-limits -Wno-maybe-uninitialized # TODO REMOVE THIS!!!!
endif
CPPFLAGS	:=
CPPFLAGS	+= -I $(IDIR)

ifdef DEBUG
	CXXFLAGS	+= -g3
#	CXXFLAGS	+= -fno-limit-debug-info # gcc doesn't use this flag only clang (works with our setup though)
else
	CXXFLAGS	+= -O3
endif

# * RULES ******************************************************************** #

all: $(NAME)

$(NAME): $(ODIR) $(OBJS)
	$(CXX) $(LDFLAGS) -o $(NAME) $(OBJS)

$(ODIR):
	$(MKDIR) $(ODIR)

-include $(DEPS)

$(ODIR)%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< -c -o $@

clean:
	$(RM) $(ODIR)
	@$(RM) tot

fclean: clean
	$(RM) $(NAME)

re: fclean all

tot: tests/timeout.cpp
	$(CXX) -Wall -Wextra -Werror -std=c++98 $< -o $@

CLOJURE_BIN	:= $(HOME)/.local/bin/clojure
WWWROOT		:= $(CURDIR)/www

install-clojure:
	@if [ ! -f "$(CLOJURE_BIN)" ]; then \
		echo "Installing Clojure CLI to ~/.local ..."; \
		curl -sL https://github.com/clojure/brew-install/releases/latest/download/linux-install.sh -o /tmp/linux-install.sh; \
		chmod +x /tmp/linux-install.sh; \
		/tmp/linux-install.sh --prefix $(HOME)/.local; \
		rm /tmp/linux-install.sh; \
	else \
		echo "Clojure already installed at $(CLOJURE_BIN)"; \
	fi

prepare-confs:
	mkdir -p conf/generated
	for f in conf/*.conf; do \
		sed 's|__WWWROOT__|$(WWWROOT)|g' $$f > conf/generated/$$(basename $$f); \
	done

.PHONY: test
test: install-clojure prepare-confs
	cd webserv-tests && PATH="$(HOME)/.local/bin:$(PATH)" clojure -M:test

.PHONY: all clean fclean re install-clojure prepare-confs
