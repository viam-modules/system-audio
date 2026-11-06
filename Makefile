OUTPUT_NAME = audio-module
BIN := build-conan/build/RelWithDebInfo/audio-module
.PHONY: build setup test clean lint conan-pkg test-asan

default: module.tar.gz

build: $(BIN)

$(BIN): conanfile.py src/* bin/* test/*
	bin/build.sh

test: $(BIN)
	cd build-conan/build/RelWithDebInfo && ctest --output-on-failure

# Build with AddressSanitizer and UndefinedBehaviorSanitizer
# Recomended runtime options:
# ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:symbolize=1 ./audio-module
# See docs for full list of runtime options:
# https://github.com/google/sanitizers/wiki/addresssanitizerflags
# https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizer#flags
# https://github.com/google/sanitizers/wiki/SanitizerCommonFlags
# https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
conan-pkg-asan:
	GTEST_DISCOVERY_TIMEOUT=60 \
	CXXFLAGS="-fsanitize=address,undefined -fsanitize-address-use-after-scope -fno-omit-frame-pointer -g" \
	CFLAGS="-fsanitize=address,undefined -fsanitize-address-use-after-scope -fno-omit-frame-pointer -g" \
	LDFLAGS="-fsanitize=address,undefined" \
	bin/build.sh

test-asan: conan-pkg-asan
	@if [ "$$(uname)" = "Darwin" ]; then \
		echo "Running ASAN tests on macOS (leak detection not supported)"; \
		cd build-conan/build/RelWithDebInfo && \
		ASAN_OPTIONS=detect_stack_use_after_return=1:symbolize=1 ctest --output-on-failure; \
	else \
		cd build-conan/build/RelWithDebInfo && \
		ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:symbolize=1 ctest --output-on-failure; \
	fi

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
	--build=missing \
	--build="b2/*"


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
