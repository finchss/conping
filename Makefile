CC=gcc
all:
		$(CC) conping.c -o conping -W -Wall -s -Os -static
