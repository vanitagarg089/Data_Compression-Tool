CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
TARGET   = huffman_server
SRC      = backend/huffman_server.cpp

all: $(TARGET)
	@echo ""
	@echo "✅  Build successful!"
	@echo "    Run:  ./$(TARGET)"
	@echo "    Open: http://localhost:8080"
	@echo ""

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

.PHONY: all clean
