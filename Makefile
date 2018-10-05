all : netfileserver.c
	gcc -Wall -pthread -g -o netfileserver netfileserver.c
clean:
	rm -f netfileserver
