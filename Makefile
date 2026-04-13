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

SRCS	+= Connection.cpp
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
CPPFLAGS	:= -I $(IDIR)

ifdef DEBUG
	CXXFLAGS	+= -g3
	CXXFLAGS	+= -fno-limit-debug-info
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

fclean: clean
	$(RM) $(NAME)

re: fclean all

.PHONY: all clean fclean re
