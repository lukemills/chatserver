#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "sockettome.h"

void *clientThreadOp(void *package);

int main(int argc, char **argv){
	int port, s, fd, fd2;
	void *rv;
	FILE *sockconn[2];
	pthread_t clientT;

	port = atoi(argv[1]);
	if (port < 5000) {
		fprintf(stderr, "usage: ./chat-server port Chat-Room-Names ...\n");
		fprintf(stderr, "       port must be > 5000\n");
		exit(1);
	}


	if(argc < 3){
		fprintf(stderr, "usage: ./chat-server port Chat-Room-Names ...\n");
		exit(1);
	}

	port = atoi(argv[1]);
	printf("Serving socket %d...\n", port);
	s = serve_socket(port);
	while(1){
		printf("Waiting for client to connect...\n");
		fd = accept_connection(s);
		sockconn[0] = fdopen(fd, "r");
		sockconn[1] = fdopen(fd, "w");
		printf("Client connected! fd is %d\n", fd);
		if(pthread_create(&clientT, NULL, clientThreadOp, &sockconn) != 0){
			perror("pthread_create");
			exit(1);
		}
		if(pthread_join(clientT, &rv) != 0){
			perror("pthread_join");
			exit(1);
		}
//		close(fd);
	}

	printf("Hello world!\n");
	return 0;
}

void *clientThreadOp(void *package){
	FILE **sockconn;
	int fd;
	char str[1000];

	sockconn = (FILE **) package;
	/*
	fd = *((int*) package);
	sock_in = fdopen(fd, "r");
	*/
	while(fgets(str, 1000, sockconn[0]) != NULL){
		printf("%s", str);
		fputs(str, sockconn[1]);
		fflush(sockconn[1]);
	}
}
