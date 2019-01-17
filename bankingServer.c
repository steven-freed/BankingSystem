#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>
#include "bankingServer.h"

sem_t stopSem;

void error(char* msg, int length) {
	write(STDERR, msg, length);
	exit(1);
}

int checkStop(int* stop, pthread_mutex_t* stopMutex) {
	pthread_mutex_lock(stopMutex);
	int check = *stop;
	pthread_mutex_unlock(stopMutex);
	return check;
}

char* doSocketCommand(AccountsDb* adb, DLLNode* node, char* input) {
	char* retMessage;
	char* str = input + 1;
	double amt = atof(str);
	int tmpRetVal;
	int queryFlag = 0;
	switch(input[0]) {
		case '0':
			if (createAccountEntry(adb, str)) retMessage = "Successfully created";
			else retMessage = "Failed to create";
			break;
		case '1': // serve
			if (node->ae != NULL) retMessage = "Failed to serve, must sign out of current session first";
			else if ((node->ae = getAccountsDb(adb, str, 1)) == NULL) retMessage = "Failed to serve, that account has not been created";
			else if (signinAccountEntry(node->ae)) retMessage = "Successfully signed in";
			else retMessage = "Failed to serve, account is already being served";
			break;
		case '2': // deposit
			if (node->ae == NULL) retMessage = "Failed to deposit, must be signed in";
			else { retMessage = "Successfully deposited";
				node->ae->balance += amt;
			}
			break;
		case '3': // withdraw
			if (node->ae == NULL) retMessage = "Failed to withdraw, must be signed in";
			else if (node->ae->balance < amt) retMessage = "Failed to withdraw, insufficient funds";
			else { retMessage = "Successfully withdrawn";
				node->ae->balance -= amt;
			}
			break;
		case '4': // query
			if (node->ae == NULL) retMessage = "Failed to query, must be signed in";
			else { retMessage = "Successfully queried"; queryFlag = 1; }
			break;
		case '5': // end
			if (node->ae == NULL) retMessage = "Failed to end, must be signed in";
			else if (signoutAccountEntry(node->ae)) { retMessage = "Successfully ended"; node->ae = NULL;
			} else retMessage = "Failed to end but you're signed in so RIP, gl recovering your account";
			break;
		case '6': // quit
			retMessage = "Successfully quit";
			break;
		default: 
			retMessage = "Invalid command";
	}
	int rsLen = 0; for (rsLen = 0; retMessage[rsLen] != '\0'; rsLen++) {} 
	char* retStr = (char*)malloc(rsLen + (queryFlag ? 400 : 0));
	if (queryFlag) { 
		char* tmpStr = (char*)malloc(rsLen);
		strcpy(tmpStr, retMessage);
		snprintf(retStr, rsLen + 400, "%s, %lf", tmpStr, node->ae->balance);
		free(tmpStr);
	} else strcpy(retStr, retMessage);
	return retStr;
}

int serverWorkerSend(int sockfd, char* msg) {
	int length;
	for (length = 0; msg[length] != '\0'; length++) {}
	length++;
	int written = 0;
	int n = 0;
	int count = 1;
	errno = 0;
	while (written < length && n >= 0 && errno == 0) {
		n = write(sockfd, &(msg[written]), length - written);
		count++; written += n;
	}
	return written == length ? count : -1;
}

int removeWorker(DLL* dll, DLLNode* node) {
	int working;
	pthread_mutex_lock(&node->workingMutex);
        working = node->working;
	node->working = 1;
        pthread_mutex_unlock(&node->workingMutex);
	if (working) return 0;
	serverWorkerSend(node->socketFd, "Server quit");
	close(node->socketFd);
        removeDLL(dll, node, 0);
        freeDLLNode(node);
	return 1;
}

void* serverWorker(void* args) {
	void** params = (void**)args;
	int* stop = (int*)params[0];
	pthread_mutex_t* stopMutex = (pthread_mutex_t*)params[1];
	DLLNode* node = (DLLNode*)params[2];
	DLL* dll = (DLL*)params[3];
	AccountsDb* adb = params[4];
	char buffer[512];
	int bufferLength = 512;
	int bufferStrLen = 0;
	int bufferStart = 0;
	AccountEntry* ae = NULL;
	while(!checkStop(stop, stopMutex)) {
		int containsNullTerminator = 0; 
		int n;
		while (!containsNullTerminator) {
                	n = read(node->socketFd, &buffer[bufferStart], bufferLength - bufferStart);
			if (errno == EINTR) { error("Error, invalid read from socket.\n", 34); break; }
			else if (errno) { error("Error, invalid socket state.\n", 30); break; }
                	if (n < 0) { error("Error, could not read from socket.\n", 36); break; }
			bufferStart += n;
			for (bufferStrLen = bufferStrLen; bufferStrLen < bufferStart; bufferStrLen++) {
				if (buffer[bufferStrLen] == '\0') { containsNullTerminator = 1; break; }
			}
			if (n == 0) break;
		}
		if (!containsNullTerminator) break;
		char* msg = doSocketCommand(adb, node, buffer);
		if (msg != NULL) {
			serverWorkerSend(node->socketFd, msg);
			free(msg);
		}
		int i;
		for (i = bufferStrLen; i < bufferStart; i++)
			buffer[i - bufferStrLen] = buffer[i];
		bufferStart -= bufferStrLen + 1;
		bufferStrLen = 0;
	}
	removeWorker(dll, node);
	if (node->ae != NULL) signoutAccountEntry(node->ae);
	free(args);
        return NULL;
}

void* serverAccept(void* args) {
	void** params = (void**)args;
	int* ret = params[0];
	int* sockfd = params[1];
	struct sockaddr* cli_addr = params[2];
	int* clilen = params[3];
	*ret = accept(*sockfd, cli_addr, clilen);
}

void* serverInstance(void* args) {
	void** params = args;
	int* port = (int*)params[0];
	AccountsDb* adb = params[1];
	int* stop = params[2];
	pthread_mutex_t* stopMutex = params[3];
	pthread_t* acceptThreadCopy = params[4];

	int sockfd, portno, clilen;
	char buffer[256];
	struct sockaddr_in serv_addr, cli_addr;
	int n;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) 
		error("Error, could not open socket\n", 30);
	bzero((char *) &serv_addr, sizeof(serv_addr));
	portno = *port;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portno);
	if (bind(sockfd, (struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
		error("Error on binding, try a different port.\n", 41);
	listen(sockfd, 5);
	clilen = sizeof(cli_addr);
	// server ready to recieve requests soon
	write(STDOUT, "[Server] online, printing db every 15 seconds.\n", 47);
	DLL* socketDLL = createDLL();
	while (!checkStop(stop, stopMutex)) {
		DLLNode* newNode = createDLLNode();
		int threadRet;
		void* tcParams[4];
		tcParams[0] = &threadRet;
		tcParams[1] = &sockfd;
		tcParams[2] = &cli_addr;
		tcParams[3] = &clilen;
		pthread_create(acceptThreadCopy, NULL, serverAccept, tcParams);
		pthread_join(*acceptThreadCopy, NULL);
		newNode->socketFd = threadRet;
		if (checkStop(stop, stopMutex)) { freeDLLNode(newNode); break; }
		void** swParams = (void**)malloc(sizeof(void*) * 5);
		swParams[0] = stop;
		swParams[1] = stopMutex;
		swParams[2] = newNode;
		swParams[3] = socketDLL;
		swParams[4] = adb;
		addDLL(socketDLL, newNode);
		pthread_create(&newNode->thread, NULL, serverWorker, swParams);
	}
	while (socketDLL->length > 0) {
		DLLNode* cur = socketDLL->first;
		DLLNode* prev;
		while (cur != NULL) {
			pthread_mutex_lock(&socketDLL->changeMutex);
			prev = cur;
			cur = cur->next;
			removeWorker(socketDLL, prev);
			pthread_mutex_unlock(&socketDLL->changeMutex);
		}
	}
	freeDLL(socketDLL);
	close(sockfd);
	return NULL;
}

void* printLoop(void* args) {
	void** params = (void**) args;
	AccountsDb* adb = (AccountsDb*)params[0];
	int* stop = (int*)params[1];
	pthread_mutex_t* stopMutex = (pthread_mutex_t*)params[2];
	int* sleepTime = (int*)params[3];
	FILE* fp = fdopen(STDOUT, "w");

	int count;
	for(count = 0; !checkStop(stop, stopMutex); count = count >= *sleepTime ? 0 : count + 1) {
		sleep(1);
		if (count + 1 == *sleepTime) {
			fprintf(fp, "---printing db, entries: %d---\n", adb->length);
			AccountEntry* cur = adb->first;
			while (cur != NULL) {
				pthread_mutex_lock(&cur->servedMutex);
				int inSession = cur->served;
				pthread_mutex_unlock(&cur->servedMutex);
				fprintf(fp, "%s\t%lf\t%s\n", cur->name, cur->balance, inSession ? "IN SESSION" : "");
				cur = cur->next;
			}
		}
			
	}
	fclose(fp);
	return NULL;
}

void stopLoop(int dummy) {
	write(STDOUT, "\n", 2);
	if (sem_post(&stopSem) == -1)
		exit(0);
	int val = -1;
	sem_getvalue(&stopSem, &val);
}

int main(int argc, char *argv[] ) {
	if (argc < 2) { write(STDERR, "Invalid parameters, use format './[executable] [port number]\n", 61); return 0; }
	char* endPortArt;
	int port = atoi(argv[1]);
	if (port < 8192 || port > 65535) { write(STDERR, "Invalid port, must be between 8193 and 65534.\n", 47); return 0; }

	AccountsDb* adb = createAccountsDb();
	int stop = 0;
	int printLoopTime = 15;
	pthread_mutex_t stopMutex;
	pthread_mutex_init(&stopMutex, NULL);
	pthread_t serverAcceptThread;

	// printout loop
	void* printLoopArgs[4];
	printLoopArgs[0] = adb;
	printLoopArgs[1] = &stop;
	printLoopArgs[2] = &stopMutex;
	printLoopArgs[3] = &printLoopTime; // seconds
	pthread_t printLoopThread;
	pthread_create(&printLoopThread, NULL, printLoop, printLoopArgs);
	// stop loop
	sem_init(&stopSem, 0, 0);
	signal(SIGINT, stopLoop);
	sigaction(SIGPIPE, &(struct sigaction){SIG_IGN}, NULL);

	void* serverArgs[5];
	serverArgs[0] = &port;
	serverArgs[1] = adb;
	serverArgs[2] = &stop;
	serverArgs[3] = &stopMutex;
	serverArgs[4] = &serverAcceptThread;
	pthread_t serverThread;
	pthread_create(&serverThread, NULL, serverInstance, (void*)serverArgs);
	int stopSemInt = -1;
	sem_getvalue(&stopSem, &stopSemInt);
	sem_wait(&stopSem);
	pthread_mutex_lock(&stopMutex);
	stop = 1;
	pthread_mutex_unlock(&stopMutex);
	pthread_cancel(serverAcceptThread);
	pthread_join(printLoopThread, NULL);
	pthread_join(serverThread, NULL);
	freeAccountsDb(adb);
	return 0;
}

AccountsDb* createAccountsDb() {
	AccountsDb* adb = (AccountsDb*)malloc(sizeof(AccountsDb));
	adb->first = NULL;
	adb->length = 0;
	pthread_mutex_init(&adb->mutex, NULL);
	return adb;
}

void freeAccountsDb(AccountsDb* adb) {
	if (adb == NULL) return;
	AccountEntry* cur = adb->first;
	int i; for (i = 0; i < adb->length; i++) {
		AccountEntry* next = cur->next;
		if (cur != NULL) { pthread_mutex_destroy(&cur->servedMutex); free(cur); }
		cur = next;
	}
	pthread_mutex_destroy(&adb->mutex);
	free(adb);
}

int cmpName(char* x, char* y) {
	if (x == NULL || y == NULL) return x == y ? 0 : x == NULL ? -1 : 1;
	return strcmp(x, y);
}

AccountEntry* getAccountsDb(AccountsDb* adb, char* name, int lock) {
	if (adb == NULL) return NULL;
	if (lock) pthread_mutex_lock(&adb->mutex);
	AccountEntry* cur = adb->first;
	while (cur != NULL) {
		if (!cmpName(cur->name, name)) break;
		cur = cur->next;
	}
	if (lock) pthread_mutex_unlock(&adb->mutex);
	return cur;
}

int createAccountEntry(AccountsDb* adb, char* name) {
	pthread_mutex_lock(&adb->mutex);
	while (1) {
		if (adb == NULL || getAccountsDb(adb, name, 0) != NULL) break;
		AccountEntry* ae = (AccountEntry*)malloc(sizeof(AccountEntry));
		strcpy(ae->name, name);
		pthread_mutex_init(&ae->servedMutex, NULL);
		ae->balance = 0; ae->served = 0;
		ae->next = adb->first; adb->first = ae;
		adb->length++;
		pthread_mutex_unlock(&adb->mutex);
		return 1;
	}
	pthread_mutex_unlock(&adb->mutex);
	return 0;
}

int signinAccountEntry(AccountEntry* ae) {
	if (ae == NULL) return -1;
	int ret = 0;
	pthread_mutex_lock(&ae->servedMutex);
	if (!ae->served) { ae->served = 1; ret = 1; }
	pthread_mutex_unlock(&ae->servedMutex);
	return ret;
}

int signoutAccountEntry(AccountEntry* ae) {
	if (ae == NULL) return -1;
	int ret = 0;
	pthread_mutex_lock(&ae->servedMutex);
        if (ae->served) { ae->served = 0; ret = 1; }
        pthread_mutex_unlock(&ae->servedMutex);
	return ret;
}

DLL* createDLL() {
	DLL* dll = (DLL*)malloc(sizeof(DLL));
	dll->first = NULL; dll->last = NULL;
	dll->length = 0;
	pthread_mutex_init(&dll->changeMutex, NULL);
	return dll;
}

void freeDLL(DLL* dll) {
	if (dll == NULL) return;
	pthread_mutex_destroy(&dll->changeMutex);
	free(dll);
}

DLLNode* createDLLNode() {
	DLLNode* node = (DLLNode*)malloc(sizeof(DLLNode));
	node->prev = NULL; node->next == NULL;
	node->ae = NULL;
	pthread_mutex_init(&node->workingMutex, NULL);
	node->working = 0;
	return node;
}

void freeDLLNode(DLLNode* node) {
	if (node == NULL) return;
	pthread_mutex_destroy(&node->workingMutex);
	free(node);
}

int addDLL(DLL* dll, DLLNode* node) {
	if (dll == NULL || node == NULL) return 0;
	pthread_mutex_lock(&dll->changeMutex);
	if (dll->first == NULL) { dll->first = dll->last = node;
	} else {
		dll->last->next = node;
		node->prev = dll->last;
		dll->last = node;
	}
	dll->length++;
	pthread_mutex_unlock(&dll->changeMutex);
	return 1;
}

int removeDLL(DLL* dll, DLLNode* node, int lock) {
	if (dll == NULL || node == NULL) return 0;
	if (lock) pthread_mutex_lock(&dll->changeMutex);
	if (node == dll->first) dll->first = node->next;
	if (node == dll->last) dll->last = node->prev;
	if (node->next != NULL) node->next->prev = node->prev;
	if (node->prev != NULL) node->prev->next = node->next;
	dll->length--;
	if (lock) pthread_mutex_unlock(&dll->changeMutex);
	return 1;
}

