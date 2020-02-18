CC = gcc
CFLAGS =-Wall -pedantic #Show all reasonable warnings
LDFLAGS = 

all: Client Server

Client : Client.o

Server : Server.o

Client.o: Client.c

Server.o: Server.c

clean:
rm -f: *.o

.PHONY: all clean 