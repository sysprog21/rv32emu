#!/usr/bin/env bash

# To enable multi-arch image build
# https://www.docker.com/blog/how-to-rapidly-build-multi-architecture-images-with-buildx/
docker buildx create --name cross-platform-builder --use --bootstrap

docker buildx build --progress=plain --push --platform linux/amd64,linux/arm64/v8 --tag sysprog21/rv32emu-gcc -f Dockerfile-gcc . 2>&1 | tee build-gcc.log
rm build-gcc.log

docker buildx build --progress=plain --push --platform linux/amd64,linux/arm64/v8 --tag sysprog21/rv32emu-sail -f Dockerfile-sail . 2>&1 | tee build-sail.log
rm build-sail.log
