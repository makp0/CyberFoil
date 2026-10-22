# A POSIX host compiler is sufficient; no Switch SDK or test framework required.
CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -pthread
ROOT := ..
OUT ?= /tmp/cyberfoil-lan-discovery-tests
.PHONY: test
test:
	$(CXX) $(CXXFLAGS) -I$(ROOT)/include console_inventory_tests.cpp -o $(OUT)-inventory
	$(OUT)-inventory
	$(CXX) $(CXXFLAGS) -I$(ROOT)/include ui_dispatcher_tests.cpp -o $(OUT)-ui
	$(OUT)-ui
	$(CXX) $(CXXFLAGS) -I$(ROOT)/include install_queue_tests.cpp -o $(OUT)-queue
	$(OUT)-queue
	$(CXX) $(CXXFLAGS) -I$(ROOT)/include lan_discovery_tests.cpp $(ROOT)/source/util/lan_discovery.cpp $(ROOT)/source/util/lan_discovery_scan.cpp -o $(OUT)
	$(OUT)
	$(CXX) $(CXXFLAGS) -DAPP_VERSION=\"test\" -I$(ROOT)/include remote_connection_tests.cpp $(ROOT)/source/util/config.cpp -o $(OUT)-remote
	$(OUT)-remote
