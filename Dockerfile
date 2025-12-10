# If this is changed, need to run
# docker build -f Dockerfile  -t ghcr.io/viam-modules/audio/viam-audio:0.0.x .
# docker push ghcr.io/viam-modules/audio/viam-audio:0.0.x
# and update the version number in .github/workflows/build-publish.yml

FROM ubuntu:jammy

ENV HOME=/root
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update

RUN apt-get -y dist-upgrade

RUN apt-get -y --no-install-recommends install \
    build-essential \
    ca-certificates \
    curl \
    g++ \
    git \
    gnupg \
    gpg \
    less \
    ninja-build \
    software-properties-common \
    sudo \
    cmake \
    wget


# Add the public key for the llvm repository to get the correct clang version
RUN bash -c 'wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key|apt-key add -'
RUN apt-add-repository -y 'deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-15 main'

# Add public key and repository to get cmake 3.25+
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - > /usr/share/keyrings/kitware-archive-keyring.gpg
RUN echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ jammy main' > /etc/apt/sources.list.d/kitware.list
RUN apt-get update

RUN apt-get -y --no-install-recommends install -t llvm-toolchain-jammy-15 \
    clang-format

RUN apt-get -y install cmake
RUN cmake --version

RUN apt-get -y --no-install-recommends install \
    python3.10 \
    python3.10-venv \
    python3-pip


RUN python3 -m pip install conan
RUN conan profile detect

# NOTE: If you update the `viam-cpp-sdk` dependency here, it
# should also be updated in `bin/setup.{sh,ps1}`, and in the conanfile.py.
RUN git clone https://github.com/viamrobotics/viam-cpp-sdk.git \
    --branch releases/v0.21.0 --depth=1 && \
    cd viam-cpp-sdk && \
    conan create . \
    --build=missing \
    -o:a "&:shared=False" \
    -s:a build_type=Release \
    -s:a compiler.cppstd=17
