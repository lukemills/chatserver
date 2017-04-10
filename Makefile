#CS 360 Lab 7 Makefile

CC = gcc 

CFLAGS = -g

EXECUTABLES: chat_server

all: $(EXECUTABLES)

char_server: char_server.o
	$(CC) -o chat_server chat_server.o 

chat_server.o: chat_server.c
	$(CC) $(CFLAGS) -c chat_server.c

#make clean will rid your directory of the executable,
#object files, and any core dumps you've caused
clean:
	rm chat_server
	rm chat_server.o

