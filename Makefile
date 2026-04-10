CXX = g++
CXXFLAGS = -g -Wall -std=c++17

supervise : supervise.cpp
	$(CXX) $(CXXFLAGS) supervise.cpp -o $@

clean :
	rm -f supervisor
