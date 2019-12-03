#!/bin/sh

# script to build a static binary using an alpine linux docker
# Expects this directory mounted as the src directory
# docker run --rm -it -v "$PWD":/src alpine /src/build_static.sh
# see build_standalone_docker.sh

echo Running $0

if ! grep -q Alpine /etc/issue; then
	echo "Script intended for Alpine Linux, see build_standalone_docker.sh"
	exit 1
fi

apk update
apk add su-exec alpine-sdk git zeromq-dev libsodium-static zlib-dev

git clone --depth=1 https://github.com/zeromq/zmqpp
cd zmqpp
make
make install
cd /
git clone --depth=1 https://github.com/wandsdn/minisat
cd minisat
make
make install

cd /
cp -r src build
cd build
make clean
make standalone

# Copy back exec using the userid and group as the owner of the /src folder
su-exec $(stat -c '%u' /src):$(stat -c '%g' /src) mkdir -p /src/build/standalone/bin/
su-exec $(stat -c '%u' /src):$(stat -c '%g' /src) cp /build/build/standalone/bin/minisat-zmq /src/build/standalone/bin/

echo
echo "Done"
echo "Standalone binary saved as ./build/standalone/bin/minisat-zmq"
echo
echo "Returning to a shell within the docker"
echo "Type exit to exit"
sh
echo
echo "Exiting ..."
