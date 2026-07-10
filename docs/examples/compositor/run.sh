#!/bin/sh
# Start the installed compositor and two client processes.  A TTF font
# passed to fbcompd (or via FBCOMPD_FONT) gives window titles; without
# one the compositor still runs, just titleless.
minix-service up /service/fb -dev /dev/fb0 >/dev/null 2>&1
echo "shm check:"; ./shmtest
FBCOMPD_FONT=${FBCOMPD_FONT:-/mnt/font.ttf} /usr/bin/fbcompd &
sleep 2
./client_hello &
sleep 1
./client_clock &
sleep 1
./client_keys &		# started last, so it is topmost and has keyboard focus
echo "compositor (/usr/bin/fbcompd) + 3 clients started"
