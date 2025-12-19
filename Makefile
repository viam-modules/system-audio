OUTPUT_NAME = audio-module
BIN := build-conan/build/RelWithDebInfo/audio-module

DOCKER_REGISTRY := ghcr.io
DOCKER_IMAGE := viam-modules/audio/viam-audio
DOCKER_VERSION := $(shell grep -oP 'viam-audio:\K[0-9]+\.[0-9]+\.[0-9]+' Dockerfile | head -1 || echo "latest")

.PHONY: build setup test clean lint conan-pkg docker-arm64-ci

default: module.tar.gz

build: $(BIN)

$(BIN): conanfile.py src/* bin/* test/*
	bin/build.sh

test: $(BIN)
	cd build-conan/build/RelWithDebInfo && ctest --output-on-failure

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

# Docker target for CI
docker-arm64-ci:
	docker build --platform linux/arm64 \
		-t $(DOCKER_REGISTRY)/$(DOCKER_IMAGE):$(DOCKER_VERSION) \
		-t $(DOCKER_REGISTRY)/$(DOCKER_IMAGE):latest \
		-f Dockerfile .
	docker push $(DOCKER_REGISTRY)/$(DOCKER_IMAGE):$(DOCKER_VERSION)
	docker push $(DOCKER_REGISTRY)/$(DOCKER_IMAGE):latest
