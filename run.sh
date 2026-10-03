#!/bin/sh
# Lance l'emulateur (Linux). Options supplementaires : voir front/README.md
cd "$(dirname "$0")" && exec ./bin/aka-front "$@"
