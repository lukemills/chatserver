#CS 360 Lab 9 Makefile 
CC = gcc 
CFLAGS = -g
SOCKLIB = sockettome.o

INCLUDES = -I/home/plank/cs360/notes/Sockets -I/home/plank/cs360/include

EXECUTABLES: chat_server

all: $(EXECUTABLES)

# sockettome.o: 
# 	$(CC) $(CFLAGS) /home/plank/cs360/notes/Sockets/sockettome.c
sockettome.o:
	$(CC) $(CFLAGS) $(INCLUDES) -c /home/plank/cs360/notes/Sockets/sockettome.c

chat_server.o: chat_server.c
	$(CC) $(CFLAGS) $(INCLUDES) -c chat_server.c

chat_server: chat_server.o sockettome.o
	$(CC) -o chat_server chat_server.o /home/plank/cs360/objs/libfdr.a sockettome.o -lpthread

clean:
	rm chat_server
	rm chat_server.o

#gcc  -g -I/home/plank/cs360/notes/Sockets -I/home/plank/cs360/include -c chat_server.c
#gcc  -o chat_server chat_server.o /home/plank/cs360/objs/libfdr.a  sockettome.o -lpthread
