CXX=g++
CXXFLAGS= -g -std=c++0x
INCLUDE = ./include
LIBDIR = ./lib
LIBS = -lpthread -lwebsockets -ljson
OUTFILE = ws_server

all: ws.cpp
	$(CXX) $(CXXFLAGS) -o $(OUTFILE) $^ -I$(INCLUDE) $(LIBS)

clean:
	rm ws_server -rf

	
