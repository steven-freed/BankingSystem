#ifndef BANKINGCLIENT_H
#define BANKINGCLIENT_H

// Fd for std because ilabs use std as a stream (rather than fd)
#define STDIN 0
#define STDOUT 1
#define STDERR 2

// Command flags sent to server
typedef enum _Cmd {
	create = 0,
	serve = 1,
	deposit = 2,
	withdraw = 3,
	query = 4,
	end = 5,
	quit = 6,
} Cmd;

typedef struct _acc {
	int quit;
	int inSession; 
	char* account;
	pthread_mutex_t mutex;
} Account;

void writeSession(Account*, int);
int readSession(Account*);
int parseResponse(Account*, int*, char*);
void* listenForCmd(void*); // listens to client for commands and writes to server
void* listenToServ(void*); // listens to server for responses
void conToServ(void*); // connects to server
int isProperAmount(char*); // checks for proper formatted double amount

#endif
