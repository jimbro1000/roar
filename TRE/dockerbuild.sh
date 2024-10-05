#!/bin/sh
rmdir ./tre
docker buildx build . -t xroar/tre:12.7
