#!/bin/bash

cd ../..

failures=""

docker build -t criu-openj9-ubuntu-test:latest -f scripts/build/Dockerfile.openj9-ubuntu .
docker run --rm --privileged criu-openj9-ubuntu-test:latest
if ! docker run --rm --privileged criu-openj9-ubuntu-test:latest; then
	failures="$failures ubuntu"
fi

docker build -t criu-openj9-alpine-test:latest -f scripts/build/Dockerfile.openj9-alpine .
docker run --rm --privileged criu-openj9-alpine-test:latest
if ! docker run --rm --privileged criu-openj9-alpine-test:latest; then
	failures="$failures alpine"
fi

if [ -n "$failures" ]; then
	echo "Tests failed on $failures"
	exit 1
fi
