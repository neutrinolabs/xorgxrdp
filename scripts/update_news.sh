#!/bin/sh

curl -s -H 'Cache-Control: no-cache' \
  https://raw.githubusercontent.com/wiki/neutrinolabs/xorgxrdp/NEWS-v0.10.md > $(git rev-parse --show-toplevel)/NEWS.md
