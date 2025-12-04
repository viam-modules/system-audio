OUTPUT_NAME = audio-module
BIN := build-conan/build/RelWithDebInfo/audio-module

.PHONY: build setup test clean

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

# Both the commands below need to source/activate the venv in the same line as the
# conan call because every line of a Makefile runs in a subshell
conan-pkg:
	test -f ./venv/bin/activate && . ./venv/bin/activate; \
	conan create . \
	-o:a "viam-cpp-sdk/*:shared=False" \
	-s:a build_type=Release \
	-s:a compiler.cppstd=17 \
	--build=missing


module: conan-pkg meta.json
	test -f ./venv/bin/activate && . ./venv/bin/activate; \
	conan install --requires=viam-audio/0.0.1 \
	-o:a "viam-cpp-sdk/*:shared=False" \
	-s:a build_type=Release \
	-s:a compiler.cppstd=17 \
	--deployer-package "&" \
	--envs-generation false
