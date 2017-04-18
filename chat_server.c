#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "dllist.h"
#include "jrb.h"
#include "sockettome.h"

typedef enum {
	success,
	nameAllocated,
	nameNotAllocated
} IFAIL;

typedef struct{
	pthread_mutex_t *room_lock;
	pthread_cond_t *room_cond;
	char *name;
	Dllist clients;
	Dllist messages;
} Room;

typedef struct{
	int fd;
	Room *inRoom;
	char *name;
	FILE *sockout;
	FILE *sockin;
	Dllist listPtr;
} Client;

typedef struct {
	Client *client;
	JRB rooms;
} startClient;

/* Prototypes */
void *clientThreadOp(void *package);
int instantiateClient(JRB t, Client *c);
void destroyClient(Client *c, int onList);
void *roomThreadOp(void *package);
int sendMessage(Client *client, char *message);
void handleClientLeft(Client *client);

int main(int argc, char **argv){
	int i, numRooms, port, s, fd, fd2;
	void *rv;
	FILE *sockconn[2];
	JRB rooms, jtmp;
	Room *rtmp;
	Client *ctmp;
	startClient sctmp;
	pthread_t clientT, roomT;

	if(argc < 3){
		fprintf(stderr, "usage: ./chat-server port Chat-Room-Names ...\n");
		exit(1);
	}

	port = atoi(argv[1]);
	if (port < 5000) {
		fprintf(stderr, "usage: ./chat-server port Chat-Room-Names ...\n");
		fprintf(stderr, "       port must be > 5000\n");
		exit(1);
	}

	rooms = make_jrb();

	/* Allocate and create rooms */
	numRooms = argc - 2;
	for(i = 0; i < numRooms; i++){
		rtmp = (Room*) malloc(sizeof(Room));
		rtmp->messages = new_dllist();
		rtmp->clients = new_dllist();
		rtmp->name = argv[i+2];
		jrb_insert_str(rooms, rtmp->name, new_jval_v((void *) rtmp));
	}
	/* Spin off room threads */
	jrb_traverse(jtmp, rooms){
		rtmp = (Room*) jtmp->val.v;
		rtmp->room_lock = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
		rtmp->room_cond = (pthread_cond_t*) malloc(sizeof(pthread_cond_t));
		pthread_mutex_init(rtmp->room_lock, NULL);
		pthread_create(&roomT, NULL, roomThreadOp, ((void*) jtmp->val.v));
	}

	printf("Serving socket %d...\n", port);
	s = serve_socket(port);
	while(1){
		printf("Waiting for client to connect...\n");
		fd = accept_connection(s);
		printf("Client connected! fd is %d\n", fd);

		/* Form startClient object */
		sctmp.client = (Client*) malloc(sizeof(Client));
		sctmp.client->fd = fd;
		sctmp.client->sockin = fdopen(fd, "r");
		sctmp.client->sockout = fdopen(fd, "w");
		sctmp.rooms = rooms;

		if(pthread_create(&clientT, NULL, clientThreadOp, &sctmp) != 0){
			perror("pthread_create");
			exit(1);
		}
	}

	return 0;
}

void *clientThreadOp(void *package){
	JRB rooms;
	int fd;
	char str[1000];
	char *message;
	Client *client;
	IFAIL ifcode;

	/* Extract Client instance and `rooms` JRB */
	client = (Client*) ((startClient*) package)->client;
	rooms = (JRB) ((startClient*) package)->rooms;

	ifcode = instantiateClient(rooms, client); 
	if(ifcode == success){

		while(fgets(str, 1000, client->sockin) != NULL && (feof(client->sockin) == 0)){
			printf("Client typed: %s", str);
			message = (char*) malloc(sizeof(char) * (strlen(str)
						+ strlen(client->name) + 9));
			sprintf(message, "%s: %s", client->name, str);
			pthread_mutex_lock(client->inRoom->room_lock);
			printf("Locked mutex for room %s\n", client->inRoom->name);
			dll_append(client->inRoom->messages, new_jval_s(message));
			pthread_cond_signal(client->inRoom->room_cond);
			pthread_mutex_unlock(client->inRoom->room_lock);
			printf("Unlocked mutex for room %s\n", client->inRoom->name);
		}
		printf("Client left normally\n");
		handleClientLeft(client);
	}
	else if(ifcode == nameAllocated){
		printf("Freeing when name allocated\n");
		destroyClient(client, 0);
		return NULL;
	}
	else if(ifcode == nameNotAllocated){
		printf("Freeing when name not allocated\n");
		fclose(client->sockin);
		fclose(client->sockout);
		close(client->fd);
		free(client);
	}
	else {
		fprintf(stderr, "WARNING: Unknown IFCODE %d\n", ifcode);
	}
}

int instantiateClient(JRB t, Client *c){
	int check;
	char *newMessage;
	JRB jtmp;
	Dllist dtmp;
	Room *rtmp;
	char buf[500];

	if(fprintf(c->sockout, "Chat Rooms:\n") < 0){ return nameNotAllocated; }

	/* Print state of chat rooms */
	jrb_traverse(jtmp, t){
		rtmp = ((Room*) jtmp->val.v);
		if(fprintf(c->sockout, "\n%s:", rtmp->name) < 0){
			return nameNotAllocated;
		}
		pthread_mutex_lock(rtmp->room_lock);
		printf("Locked mutex for room %s\n", rtmp->name);

		/* Print clients in chat rooms */
		dll_traverse(dtmp, rtmp->clients){
			if(fprintf(c->sockout, " %s", ((Client*) dtmp->val.v)->name) < 0){
				return nameNotAllocated;
			}
		}

		pthread_mutex_unlock(rtmp->room_lock);
		printf("Unlocked mutex for room %s\n", rtmp->name);
	}

	/* Solicit client information */
	if(fprintf(c->sockout, "\n\nEnter your chat name (no spaces):\n") < 0){
		return nameNotAllocated;
	}
	if(fflush(c->sockout) == EOF || feof(c->sockout)){ return nameNotAllocated; }
	fgets(buf, 500, c->sockin); if((feof(c->sockin) != 0) || buf == NULL){
		return nameNotAllocated;
	}
	buf[strlen(buf)-1] = '\0';
	c->name = strdup(buf);

	/* Prompt user for chat room name */
	if(fprintf(c->sockout, "Enter chat room:\n") < 0){ return nameAllocated; }
	if(fflush(c->sockout) == EOF || feof(c->sockout)){ return nameAllocated; }
	fgets(buf, 500, c->sockin); if((feof(c->sockin) != 0) || buf == NULL){
		return nameAllocated;
	}
	buf[strlen(buf)-1] = '\0';
	jtmp = jrb_find_str(t, buf);
	while(jtmp == NULL){ /* Continue to prompt until user specifies valid chat room */
		if(fprintf(c->sockout, "No chat room %s\n", buf) < 0){
			return nameAllocated;
		}
		if(fflush(c->sockout) == EOF || feof(c->sockout)){
			return nameAllocated;
		}
		fgets(buf, 500, c->sockin); 
		if((feof(c->sockin) != 0) || buf == NULL){ return nameAllocated; }
		buf[strlen(buf)-1] = '\0';
		jtmp = jrb_find_str(t, buf);
	}
	c->inRoom = ((Room*) jtmp->val.v);

	newMessage = (char *) malloc(sizeof(char) * (strlen(c->name)+14));
	sprintf(newMessage, "%s has joined\n", c->name);

	/* Append the client to the clients Dllist, and add new client message to 
	 * the messages Dllist */
	pthread_mutex_lock(c->inRoom->room_lock);
	printf("Locked mutex for room %s\n", rtmp->name);
	dll_append(c->inRoom->clients, new_jval_v(c));
	dll_append(c->inRoom->messages, new_jval_s(newMessage));
	c->listPtr = c->inRoom->clients->blink;
	pthread_cond_signal(c->inRoom->room_cond);
	pthread_mutex_unlock(c->inRoom->room_lock);
	printf("Unlocked mutex for room %s\n", rtmp->name);

	return success;
}

void destroyClient(Client *client, int onList){
	close(client->fd);
	fclose(client->sockin);
	fclose(client->sockout);
	printf("Freeing name\n");
	free(client->name);
	if(onList != 0){
		printf("Deleting from Dllist\n");
		dll_delete_node(client->listPtr);
	}
	printf("Freeing client\n");
	free(client);
	printf("Done destroying client\n");
}

void *roomThreadOp(void *package){
	Dllist mdll, cdll;
	Room *r;
	r = (Room *) package;
	printf("Locked mutex for room %s\n", r->name);
	pthread_mutex_lock(r->room_lock);
	while(1){ /* When signaled, print the messages in the message queue */
		printf("Unlocked mutex for room %s\n", r->name);
		pthread_cond_wait(r->room_cond, r->room_lock);
		printf("Locked mutex for room %s\n", r->name);
		while(!dll_empty(r->messages)){ 
			mdll = r->messages->flink;
		/* Send all the messages and clean up the message queue */
			printf("Message from client: %s", jval_s(mdll->val));
			dll_traverse(cdll, r->clients){
				if(sendMessage(((Client*) jval_v(cdll->val)), jval_s(mdll->val)) < 0){
					printf("WARNING: Client is gone!\n");
				}
			}
			printf("Freeing message resources\n");
			// free(jval_s(mdll->val));
			printf("Freeing node\n");
			dll_delete_node(mdll);
		}
	}
}

int sendMessage(Client *client, char *message){
	int check;
	printf("Sending message to %s: %s", client->name, message);
	if(fputs(message, client->sockout) < 1){ return -1; }
	if(fflush(client->sockout) == EOF){ return -1; }
	printf("Done sending\n");
	return 0;
}

void handleClientLeft(Client *client){
	Room *room;
	char *message;
	room = client->inRoom;
	pthread_mutex_lock(room->room_lock);
	printf("Locked mutex for room %s\n", room->name);
	message = (char*) malloc(sizeof(char) * (strlen(client->name) + 10));
	printf("Client %s has left\n", client->name);
	sprintf(message, "%s has left\n", client->name);
	printf("Cleaning up after client\n");
	destroyClient(client, 1);
	printf("Dispatching message...\n");
	dll_append(room->messages, new_jval_s(message));
	pthread_cond_signal(room->room_cond);
	printf("...Done sending client left message\n");
	pthread_mutex_unlock(room->room_lock);
	printf("Unlocked mutex for room %s\n", room->name);
}
