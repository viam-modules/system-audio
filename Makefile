OUTPUT_NAME = audio-module
BIN := build-conan/build/RelWithDebInfo/audio-module

.PHONY: build setup test clean lint conan-pkg test-asan

default: module.tar.gz

build: $(BIN)

$(BIN): conanfile.py src/* bin/* test/*
	bin/build.sh

test: $(BIN)
	cd build-conan/build/RelWithDebInfo && ctest --output-on-failure

# Build with AddressSanitizer
# Run with following runtime options:
# ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:symbolize=1
conan-pkg-asan:
	test -f ./venv/bin/activate && . ./venv/bin/activate; \
	CXXFLAGS="-fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer -g" \
	CFLAGS="-fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer -g" \
	LDFLAGS="-fsanitize=address" \
	conan create . \
	-o:a "viam-cpp-sdk/*:shared=False" \
	-o "boost/*:without_locale=True" \
	-o "boost/*:without_stacktrace=True" \
	-s:a build_type=RelWithDebInfo \
	-s:a compiler.cppstd=17 \
	--build=boost \
	--build=missing

test-asan: conan-pkg-asan
	cd build-conan/build/RelWithDebInfo && \
	ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:symbolize=1 ctest --output-on-failure

clean:
	rm -rf build-conan/build/RelWithDebInfo module.tar.gz

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


module.tar.gz: conan-pkg meta.json
	test -f ./venv/bin/activate && . ./venv/bin/activate; \
	conan install --requires=viam-audio/0.0.1 \
	-o:a "viam-cpp-sdk/*:shared=False" \
	-s:a build_type=Release \
	-s:a compiler.cppstd=17 \
	--deployer-package "&" \
	--envs-generation false

lint:
	./bin/lint.sh
