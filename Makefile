# Define the compiler
CXX = clang++

# Define the compiler flags
CXXFLAGS = -Wall -Wextra -Wpedantic -Wconversion -fsanitize=address -std=c++23

# Define the libraries to link
LIBS = -lws2_32

# Define the source files for client and server
CLIENT_SRCS = client.cpp
SERVER_SRCS = server.cpp

# Define the output executables
CLIENT_TARGET = client
SERVER_TARGET = server

# Default rule to build both targets
all: $(CLIENT_TARGET) $(SERVER_TARGET)

# Rule to build the client
$(CLIENT_TARGET): $(CLIENT_SRCS)
	$(CXX) $(CXXFLAGS) $(CLIENT_SRCS) -o $(CLIENT_TARGET) $(LIBS)

# Rule to build the server
$(SERVER_TARGET): $(SERVER_SRCS)
	$(CXX) $(CXXFLAGS) $(SERVER_SRCS) -o $(SERVER_TARGET) $(LIBS)

# Rule to clean the build directory
clean:
	del /f $(CLIENT_TARGET).exe $(SERVER_TARGET).exe

# Rule to rebuild the project
rebuild: clean all

.PHONY: all clean rebuild

#mingw32-make -f Makefile