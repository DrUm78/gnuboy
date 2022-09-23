#!/bin/sh
# Copy gnuboy binary in /usr/games if md5 is different
if [ `md5sum /usr/games/sdlgnuboy | cut -d' ' -f1` != `md5sum sdlgnuboy | cut -d' ' -f1` ]; then
	rw
	cp -f sdlgnuboy /usr/games
	ro
fi
exec /usr/games/launchers/gb_launch.sh "$1"
