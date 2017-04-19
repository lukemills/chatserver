#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
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
void killChatServerHandler(int d);

int main(int argc, char **argv){
	int i, numRooms, port, s, fd, fd2;
	void *rv;
	FILE *sockconn[2];
	JRB rooms, jtmp;
	Room *rtmp;
	Client *ctmp;
	startClient sctmp;
	pthread_t clientT, roomT;

	/* TODO Fix signal handling or remove
	   signal(SIGINT, killChatServerHandler);
	   signal(SIGKILL, killChatServerHandler);
	 */

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

	/* Create rooms */
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
	/* Open the socket specified via command line argument and
	   continuously accept client connections and spawn off
	   client threads */
	while(1){
		printf("Waiting for client to connect...\n");
		fd = accept_connection(s);
		printf("Client connected! fd is %d\n", fd);

		/* Form startClient object and spawn client thread */
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

void killChatServerHandler(int d){
	printf("Caught signal!\n");
	return;
}

/* clientThreadOp is run on client threads. It calls instatiateClient
 * to first print the state of the chat rooms to the client and to
 * solicits information of him/her. It then continuously reads from
 * the client, appending this text to the message queue to be
 * printed to all clients via signaling the room thread's condition
 * variable. Note all read/print operations are performed via the
 * respective socket connections */
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

	/* If instantiateClient returned success, perform reads
	   from the socket */
	if(ifcode == success){
		/* Continuously read from client, appending messages 
		   to the message as they are read */
		while(fgets(str, 1000, client->sockin) != NULL 
				&& (feof(client->sockin) == 0)){
			message = (char*) malloc(sizeof(char) * (strlen(str)
						+ strlen(client->name) + 4));
			/* Create message string, lock the mutex for
			   appending message, and signal room to print message */
			sprintf(message, "%s: %s", client->name, str); 
			pthread_mutex_lock(client->inRoom->room_lock); 
			printf("Locked mutex for room %s\n", client->inRoom->name);
			printf("Message from client %s in room %s: %s\n",
					client->name, client->inRoom->name, message); 
			printf("Appending to %s's message queue and signaling",
					client->inRoom->name);
			dll_append(client->inRoom->messages, new_jval_s(message));
			pthread_cond_signal(client->inRoom->room_cond); 
			pthread_mutex_unlock(client->inRoom->room_lock);
			printf("Unlocked mutex for room %s\n", client->inRoom->name);
		}
		handleClientLeft(client); /* Read failure indicates that client left */
	}
	/* If instantiateClient failed with `nameAllocated` (client left during
	   instantiateClient and specified name), free memory accordingly */
	else if(ifcode == nameAllocated){
		printf("Client %s left before specifying chat room\n", client->name);
		destroyClient(client, 0);
	}
	/* If instantiateClient failed with `nameNotAllocated` (client left
	   during instantiateClient and didn't specify name) free memory
	   accordingly */ 
	else if(ifcode == nameNotAllocated){
		printf("Client left before specifying name\n");
		fclose(client->sockin);
		fclose(client->sockout);
		close(client->fd);
		free(client);
	}
	else {
		fprintf(stderr, "WARNING: Unknown IFCODE %d\n", ifcode);
	}
	pthread_exit(NULL);
}

/* instantiateClient prints the current state of the
 * chat rooms to the client `c`'s socket FILE * opened for
 * writing. This state includes the chat rooms and the
 * participants of each. Following this, it solicits information
 * from the client, including his/her name and the desired
 * chat room. Following successful specification of all required
 * info, a message indicating the client's entrance into the
 * specified chat room is added to the message queue, and the
 * client is in its final state for participation in the chat room */
int instantiateClient(JRB t, Client *c){
	int check;
	char *newMessage;
	JRB jtmp;
	Dllist dtmp;
	Room *rtmp;
	char buf[1000];

	/* NOTE: Checks are performed on each I/O operation here
	 * to detect client egress and handle it accordingly */

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
	fgets(buf, 1000, c->sockin); if((feof(c->sockin) != 0) || buf == NULL){
		return nameNotAllocated;
	}
	buf[strlen(buf)-1] = '\0';
	c->name = strdup(buf);

	/* Prompt user for chat room name */
	if(fprintf(c->sockout, "Enter chat room:\n") < 0){ return nameAllocated; }
	if(fflush(c->sockout) == EOF || feof(c->sockout)){ return nameAllocated; }
	fgets(buf, 1000, c->sockin); if((feof(c->sockin) != 0) || buf == NULL){
		return nameAllocated;
	}
	buf[strlen(buf)-1] = '\0';
	jtmp = jrb_find_str(t, buf);
	while(jtmp == NULL){ /* Prompt until user specifies valid chat room */
		if(fprintf(c->sockout, "No chat room %s\n", buf) < 0){
			return nameAllocated;
		}
		if(fflush(c->sockout) == EOF || feof(c->sockout)){
			return nameAllocated;
		}
		fgets(buf, 1000, c->sockin); 
		if((feof(c->sockin) != 0) || buf == NULL){ return nameAllocated; }
		buf[strlen(buf)-1] = '\0';
		jtmp = jrb_find_str(t, buf);
	}
	c->inRoom = ((Room*) jtmp->val.v);

	newMessage = (char *) malloc(sizeof(char) * (strlen(c->name)+14));
	sprintf(newMessage, "%s has joined\n", c->name);

	/* Append the client to the clients Dllist, add the 
	 * "client joined" message to the messages Dllist, and
	 * signal the resepective chat room */
	pthread_mutex_lock(c->inRoom->room_lock);
	printf("Locked mutex for room %s\n", rtmp->name);
	dll_append(c->inRoom->clients, new_jval_v(c));
	dll_append(c->inRoom->messages, new_jval_s(newMessage));
	c->listPtr = dll_last(c->inRoom->clients);
	pthread_cond_signal(c->inRoom->room_cond);
	pthread_mutex_unlock(c->inRoom->room_lock);
	printf("Unlocked mutex for room %s\n", rtmp->name);

	return success;
}

/* destroyClient frees all resources allocated to
the Client `client`, and removes it from the appropriate
dllist if onList is non-zero */
void destroyClient(Client *client, int onList){
	close(client->fd);
	fclose(client->sockin);
	fclose(client->sockout);
	free(client->name);
	if(onList != 0){
		dll_delete_node(client->listPtr);
	}
	free(client);
}

/* roomThreadOp is run for the resepective room threads.
 * It performs the dispatch of messages in the message
 * queue to all clients in the chat room for which the
 * thread is running and cleans the message queue after-
 * ward. It blocks on a condition variable, waiting for
 * another thread to signal it (indicating that there are
 * messages to be dispatched */
void *roomThreadOp(void *package){
	Dllist mdll, cdll;
	Room *r;
	r = (Room *) package;
	printf("Locked mutex for room %s\n", r->name);
	pthread_mutex_lock(r->room_lock);
	while(1){ /* Print messages in message queue when signaled */
		printf("Unlocked mutex for room %s\n", r->name);
		printf("Room %s blocking on condition variable\n", r->name);
		pthread_cond_wait(r->room_cond, r->room_lock);
		printf("Room %s signaled and active\n", r->name);
		printf("Locked mutex for room %s\n", r->name);
		/* Dispatch all messages to all clients */
		while(!dll_empty(r->messages)){ 
			mdll = dll_first(r->messages);
			printf("Sending message to all clients: %s",
					jval_s(mdll->val));
			/* Send message to all clients */
			dll_traverse(cdll, r->clients){
				if(sendMessage(((Client*) jval_v(cdll->val)),
							jval_s(mdll->val)) < 0){
					/* If client left, free client's resources */
					destroyClient((Client*) jval_v(cdll->val), 1);
				}
			}
			/* Free allocated resources for this message */
			free(mdll->val.s);
			dll_delete_node(mdll);
		}
	}
}

/* sendMessage prints the message given by `message`
 * to Client `client` via its socket FILE*, checking
 * to ensure the printing did not fail. It returns 0
 * if the subroutine was successful and -1 if the print
 * failed to indicate that the client left */
int sendMessage(Client *client, char *message){
	int check;
	if(fputs(message, client->sockout) < 1){ return -1; }
	if(fflush(client->sockout) == EOF){ return -1; }
	return 0;
}

/* handleClientLeft serves as an all-in-one interface to the
 * freeing of the resources allocated for the Client `client`
 * and the formation of a message indicating this. It calls
 * `destroyClient` to handle the freeing of allocated resources,
 * and then creates and appends a message to the chat room of
 * which `client` was in, signaling said room after appending */
void handleClientLeft(Client *client){
	Room *room;
	char *message;
	room = client->inRoom;
	pthread_mutex_lock(room->room_lock);
	printf("Locked mutex for room %s\n", room->name);
	printf("Client %s has left\n", client->name);
	message = (char*) malloc(sizeof(char) * (strlen(client->name) + 11));
	sprintf(message, "%s has left\n", client->name);
	printf("Cleaning up after client\n");
	destroyClient(client, 1);
	dll_append(room->messages, new_jval_s(message));
	pthread_cond_signal(room->room_cond);
	pthread_mutex_unlock(room->room_lock);
	printf("Unlocked mutex for room %s\n", room->name);
}
