CXX ?= cxx
CXXFLAGS ?=
CXXFLAGS += -g -O0 -Wextra -Wall -Wno-unused-parameter

LDFLAGS ?=
LDFLAGS += -framework IOKit -framework CoreFoundation

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

OBJS ?=
OBJS += src/crc32.o
OBJS += src/ps4ds.o

ps4ds: $(OBJS)
	$(CXX) $(LDFLAGS) $(OBJS) -o $@
