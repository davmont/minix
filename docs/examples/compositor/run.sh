#!/bin/sh
# Start the compositor and two independent client processes.
minix-service up /service/fb -dev /dev/fb0 >/dev/null 2>&1
echo "shm check:"; /mnt/shmtest
/mnt/fbcompd &
sleep 2
/mnt/client_hello &
sleep 1
/mnt/client_clock &
echo "compositor + 2 clients started"
