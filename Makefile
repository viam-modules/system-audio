OUTPUT_NAME = audio-module
BIN := build-conan/build/RelWithDebInfo/audio-module

.PHONY: build setup test clean lint

default: module.tar.gz

build: $(BIN)

build/build.ninja: build CMakeLists.txt
	cd build && cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ..

$(BIN): conanfile.py src/* bin/* test/*
	bin/build.sh

test: $(BIN)
	cd build-conan/build/RelWithDebInfo && ctest --output-on-failure

clean:
	rm -rf build-conan/build/RelWithDebInfo module.tar.gz

module.tar.gz: build meta.json
	cp $(BIN) $(OUTPUT_NAME)
	tar -czvf module.tar.gz \
	    $(OUTPUT_NAME) meta.json

setup:
	bin/setup.sh

lint:
	./bin/lint.sh
