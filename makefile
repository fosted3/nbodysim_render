CC=g++
CFLAGS=-c -Wall -std=c++11 -Wextra -march=native -mtune=native -DFLOAT
LDFLAGS=-lpthread -lfreeimage
EXECUTABLE=nbodysim_render
DIRS=bin/debug/ bin/release/  build/debug/ build/release/ data/ img/

all:
	make debug
	make release

debug: CFLAGS += -g -Og
debug: BIN_DIR=bin/debug
debug: BUILD_DIR=build/debug
debug: setup
debug: $(EXECUTABLE)

release: CFLAGS += -O2
release: BIN_DIR=bin/release
release: BUILD_DIR=build/release
release: setup
release: $(EXECUTABLE)

$(EXECUTABLE): $(BUILD_DIR)/main.o $(BUILD_DIR)/particle.o $(BUILD_DIR)/vector.o
	$(CC) $(BUILD_DIR)/main.o $(BUILD_DIR)/particle.o $(BUILD_DIR)/vector.o -o $(BIN_DIR)/$(EXECUTABLE) $(LDFLAGS)

$(BUILD_DIR)/main.o: src/main.cpp
	$(CC) $(CFLAGS) src/main.cpp -o $(BUILD_DIR)/main.o

$(BUILD_DIR)/particle.o: src/particle.cpp
	$(CC) $(CFLAGS) src/particle.cpp -o $(BUILD_DIR)/particle.o

$(BUILD_DIR)/vector.o: src/vector.cpp
	$(CC) $(CFLAGS) src/vector.cpp -o $(BUILD_DIR)/vector.o

setup:
	mkdir -p $(DIRS)

clean:
	rm -f build/debug/*.o build/release/*.o bin/debug/* bin/release/*

clean_data:
	rm -f data/* img/*

test: debug
	valgrind --leak-check=full --show-reachable=yes --read-var-info=yes --track-origins=yes ./bin/debug/nbodysim_render
