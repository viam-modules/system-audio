# This Dockerfile is automatically built and published by CI when changed on main branch
# See .github/workflows/docker-publish.yml

# Ubuntu 20.04 (glibc 2.31) so the linux/arm64 binary also runs on JetPack 5 Jetsons,
# Unitree robots and other older distros. Binaries built here run on any newer glibc.
FROM ubuntu:focal

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
RUN apt-add-repository -y 'deb http://apt.llvm.org/focal/ llvm-toolchain-focal-15 main'

# Add public key and repository to get cmake 3.25+
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - > /usr/share/keyrings/kitware-archive-keyring.gpg
RUN echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ focal main' > /etc/apt/sources.list.d/kitware.list
RUN apt-get update

RUN apt-get -y --no-install-recommends install -t llvm-toolchain-focal-15 \
    clang-format

RUN apt-get -y install cmake
RUN cmake --version

RUN apt-get -y --no-install-recommends install \
    python3 \
    python3-venv \
    python3-pip

# Install audio build dependencies
RUN apt-get -y --no-install-recommends install \
    libasound-dev \
    portaudio19-dev \
    libjack-dev \
    libdb5.3-dev \
    patchelf

RUN python3 -m pip install conan
RUN conan profile detect
