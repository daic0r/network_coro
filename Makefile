CXX = clang++
CXXFLAGS = -std=c++23 -g -Wall -Wextra -Wpedantic -DASIO_HAS_CO_AWAIT 
#-DASIO_ENABLE_HANDLER_TRACKING

client: client.cpp message.h
	$(CXX) $(CXXFLAGS) $< -o $@

server: server.cpp message.h
	$(CXX) $(CXXFLAGS) $< -o $@

.PHONY: clean

clean:
	rm -rf client
	rm -rf server
