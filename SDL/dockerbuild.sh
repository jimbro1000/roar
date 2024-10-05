#!/bin/sh
git clone https://github.com/libsdl-org/SDL.git

docker buildx build . -t xroar/build:12.7
