OUTPUT_NAME = audio-module
BIN := build-conan/build/RelWithDebInfo/audio-module
SOURCE_FILES := $(shell find src -type f \( -name '*.cpp' -o -name '*.hpp' \))
CPP_FILES := $(filter %.cpp,$(SOURCE_FILES))

.PHONY: build setup test clean format run-clang-tidy

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

run-clang-tidy:
	clang-tidy-19 \
        -p build \
        --config-file ./.clang-tidy \
        --header-filter=".*/viam/(ur/module|trajex)/.*" \
	$(CPP_FILES)

run-clang-check:
	clang-check -p build $(CPP_FILES)
