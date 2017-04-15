#CS 360 Lab 9 Makefile CC = gcc 
CFLAGS = -g -c
SOCKLIB = sockettome.o

INCLUDES = -I/home/plank/cs360/notes/Sockets

EXECUTABLES: chat_server

all: $(EXECUTABLES)

# sockettome.o: 
# 	$(CC) $(CFLAGS) /home/plank/cs360/notes/Sockets/sockettome.c

chat_server: chat_server.o sockettome.o
	$(CC) -o chat_server chat_server.o sockettome.o -lpthread

chat_server.o: chat_server.c /home/plank/cs360/notes/Sockets/sockettome.c
	$(CC) $(CFLAGS) $(INCLUDES) chat_server.c /home/plank/cs360/notes/Sockets/sockettome.c

clean:
	rm chat_server
	rm chat_server.o

