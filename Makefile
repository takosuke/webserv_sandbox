# **************************************************************************** #

NAME	:= webserv

# * DIRECTORIES ************************************************************** #

SDIR	:= src/
IDIR	:= inc/
ODIR	:= obj/

# * FILES ******************************************************************** #

SRCS	:=
vpath %.cpp $(SDIR)
SRCS	+= webserv.cpp

SRCS	+= ServerConnection.cpp
SRCS	+= ClientConnection.cpp
SRCS	+= EpollLoop.cpp
SRCS	+= ServerBlock.cpp
SRCS	+= utils.cpp

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
CPPFLAGS	:= -I $(IDIR) -I parser/inc/

ifdef DEBUG
	CXXFLAGS	+= -g3
	CXXFLAGS	+= -fno-limit-debug-info
else
	CXXFLAGS	+= -O3
endif

# * RULES ******************************************************************** #

all: $(NAME)

PARSER_LIB	:= parser/libparser.a

$(NAME): $(PARSER_LIB) $(ODIR) $(OBJS)
	$(CXX) -o $(NAME) $(OBJS) -L parser/ -lparser

$(PARSER_LIB):
	$(MAKE) -C parser lib

$(ODIR):
	$(MKDIR) $(ODIR)

-include $(DEPS)

$(ODIR)%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< -c -o $@

clean:
	$(RM) $(ODIR)

fclean: clean
	$(RM) $(NAME)
	$(MAKE) -C parser fclean

re: fclean all

.PHONY: all clean fclean re
