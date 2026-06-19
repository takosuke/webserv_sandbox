# **************************************************************************** #

NAME	:= autoindex

# * DIRECTORIES ************************************************************** #

SDIR	:= src/
IDIR	:= inc/
ODIR	:= obj/

# * FILES ******************************************************************** #

SRCS	:=
vpath %.cpp $(SDIR)
SRCS	+= main.cpp
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

CXXFLAGS	:= -Wall -Werror -Wextra -std=c++98 -MMD -MP
CPPFLAGS	:= -I $(IDIR)

ifdef DEBUG
	CXXFLAGS	+= -g3
	CXXFLAGS	+= -fno-limit-debug-info # gcc doesn't use this flag only clang (works with our setup though)
else
	CXXFLAGS	+= -O3
endif

# * RULES ******************************************************************** #

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(LDFLAGS) -o $(NAME) $^

$(ODIR):
	$(MKDIR) $(ODIR)

$(ODIR)%.o: %.cpp $(ODIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $< -c -o $@

-include $(DEPS)

clean:
	$(RM) $(ODIR)

fclean: clean
	$(RM) $(NAME)

re: fclean all

.PHONY: all clean fclean re
