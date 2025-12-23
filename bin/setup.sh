#!/bin/bash
# set -e: exit with errors if anything fails
#     -u: it's an error to use an undefined variable
#     -x: print out every command before it runs
#     -o pipefail: if something in the middle of a pipeline fails, the whole thing fails
#
set -euxo pipefail

OS=$(uname -s | tr '[:upper:]' '[:lower:]')

# NOTE: this is written under the assumption that it will be built in canon

if [[ ${OS} == "darwin" ]]; then
  if ! command -v brew >/dev/null; then
     echo "Brew not installed. Please install brew!"
     exit 1
  fi

  brew install portaudio

elif  [[ ${OS} == "linux" ]]; then
    sudo apt -y update && sudo apt -y upgrade && sudo apt-get install -y libasound-dev portaudio19-dev python3-venv cmake
fi

if [ ! -f "./venv/bin/activate" ]; then
  echo 'creating and sourcing virtual env'
  python3 -m venv venv && source ./venv/bin/activate
else
  echo 'sourcing virtual env'
  source ./venv/bin/activate
fi

# Set up conan
if [ ! -f "./venv/bin/conan" ]; then
  echo 'installing conan'
  python3 -m pip install conan
fi

conan profile detect || echo "Conan is already installed"

# Add Viam Conan remote to download pre-built packages
conan remote add viamconan https://viam.jfrog.io/artifactory/api/conan/viamconan --index 0 || echo "Viam conan remote already exists"
