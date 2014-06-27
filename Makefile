all:
	gcc -g -Wall fwd-client.c -o fwd-client -lssh2

clean:
	rm -rf fwd-client *~ *.o
