#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include "bankingClient.h"

void writeQuit(Account* a, int isQuitting)
{
	pthread_mutex_lock(&a->mutex);
	a->quit = isQuitting;
	pthread_mutex_unlock(&a->mutex);
}

int readQuit(Account* a)
{
	int status;
	pthread_mutex_lock(&a->mutex);
	status = a->quit;
	pthread_mutex_unlock(&a->mutex);
	
	return status;
}

/*
 *	Writes flag for session currently being served
 */
void writeSession(Account* a, int hasSession)
{
	pthread_mutex_lock(&a->mutex);
	a->inSession = hasSession;
	pthread_mutex_unlock(&a->mutex);
}

/*
 *	Reads current session data for account being served
 */
int readSession(Account* a)
{
	int sessionStatus;
	pthread_mutex_lock(&a->mutex);
	sessionStatus = a->inSession;
	pthread_mutex_unlock(&a->mutex);

	return sessionStatus;
}

/*
 *	Parses server response
 *		- 'S' for success 'F' for Fail (1st byte of response)
 *		- msg contains the command the server was given
 *		- Returns 1 if quitting client 0 if not
 */
int parseResponse(Account* a, int* sockfd, char* res)
{
	char sorf = res[0]; 
	char* msg = strchr(res, ' ');
	
	if (sorf == 'S' && strstr(msg, "signed in") != NULL)
	{
		writeSession(a, 1);
	} else if (sorf == 'S' && strstr(msg, "end") != NULL){
		writeSession(a, 0);
	} else if (sorf == 'F' && strstr(msg, "signed in") != NULL) {
		writeSession(a, 0);
	} else if (sorf == 'S' && strstr(msg, "quit") != NULL) {
		return res[1] == 'e' ? -1 : 1;
	}	
		return 0;
}

/*
 *	Checks for an incorrectly formatted double
 *		- returns 1 if amount contains chars
 *		- returns 0 if amount contains only a double value
 */
int isProperAmount(char* amount)
{
	
	if (amount[0] == '-')
	{
		return 1;
	}

	char* strAmount;
	strtod(amount, &strAmount);
	
	int len = strlen(strAmount);
	int i = 0;
	
	while (i < len)
	{
		// double contains chars 
		if (isalpha(strAmount[i]) != 0)
		{
			return 1;
		}
	}
		return 0;
}

/*
 *	Connects to server socket via TCP and spawns threads
 *		- thread 1 listens for client commands and sends them to server
 *		- thread 2 listens for server responses 
 */
void conToServ(void* args)
{
	FILE* fp = fdopen(STDOUT, "w");
	FILE* fperr = fdopen(STDERR, "w");
	void** params = args;
	int* port = params[0];
	char* host = params[1];
	Account* session = params[2];

	int sockfd, portno;
	struct sockaddr_in serv_addr;
	struct hostent *server;

	char buffer[512];
	portno = *port;
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
	{ 
		fprintf(fperr, "ERROR opening socket\n");
	}
	server = gethostbyname(host);
	if (server == NULL) 
	{
		fprintf(fperr,"ERROR, no such host\n");
		exit(0);
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr, 
		(char *)&serv_addr.sin_addr.s_addr,
		server->h_length);
	serv_addr.sin_port = htons(portno);
	
	fprintf(fp, "Waiting to connect to server...\n");
	do {sleep(3);} while (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0); 
		
	fprintf(fp, "Successful Connection.\n");

	void* cmdArgs[3];
	cmdArgs[0] = &sockfd;
	cmdArgs[1] = session;

	void* resArgs[3];
	resArgs[0] = &sockfd;
	resArgs[1] = session;

	pthread_t cmdThread;
	pthread_t resThread;
	pthread_create(&cmdThread, NULL, listenForCmd, (void*) cmdArgs);
	pthread_create(&resThread, NULL, listenToServ, (void*) resArgs);
	pthread_join(cmdThread, NULL);
	pthread_join(resThread, NULL);
	
	fclose(fp);
	fclose(fperr);
}

/*
 *	Listens for client commands and sends them to server
 */
void* listenForCmd(void* args)
{
	FILE* fp = fdopen(STDOUT, "w");
	FILE* fperr = fdopen(STDERR, "w");
	
	char buffer[512]; // buffer from stdin client
	int n; // number of bytes
	
	void** params = args;
	int sockfd = *((int*)params[0]);
	Account* session = params[1];
	
	int isServing = 0; // serving flag
	char* servingAccount = (char*) malloc(sizeof(char)*256); // currently served account

	fprintf(fp, "Please enter commands:\n");
	
	while(1) 
	{

		isServing = readSession(session);

		bzero(buffer, 512);
	
		int bytesRead;	
		fprintf(fp, "> ");
		fflush(fp);
		bytesRead = read(STDIN, buffer, 512);
		while (bytesRead < 0);
		buffer[bytesRead-1] = '\0'; 
		
		if (readQuit(session) == 1) break;
		// splits command from account name or amount
		// splits on space
		char tempBuffer[512]; 
		char* cmdToken = NULL; // account name or amount
		int accLen = 0; // accout name length

		strcpy(tempBuffer, buffer);
		
		// split on space if command contains space
		if (strstr(buffer, " ") != NULL)
		{
 			cmdToken = strchr(tempBuffer, ' '); 
			accLen = strlen(cmdToken);
		} 
		
		// null terminates command so it is not full buffer size
		if (cmdToken != NULL)
		{
			*cmdToken = '\0'; 
		}
		
		int isValidCmd = 0; // valid command flag
		Cmd cmd; // enum for command
		
		/*
 		*	Checks for all commands
 		*	throwing errors specific to user entered commands
 		*	keeps enum flag and track of account being served
 		*/
		if (strcmp(tempBuffer, "create") == 0 && isServing == 0 && accLen <= 256 && cmdToken != NULL)
		{
			cmd = create;
			isValidCmd = 1;
		} else if (strcmp(tempBuffer, "serve") == 0 && isServing == 0 && accLen <= 256 && cmdToken != NULL) {
			cmd = serve;
			isValidCmd = 1;
			isServing = 1;
			strcpy(servingAccount, (cmdToken+1));
		} else if (strcmp(tempBuffer, "deposit") == 0 && isServing == 1 && cmdToken != NULL) {
			// checks for correct formatting of amount
			int amountCheck = isProperAmount((cmdToken+1));	
			if (amountCheck == 0)
			{	
				cmd = deposit;
				isValidCmd = 1;
			} else {
				fprintf(fp, "ERROR: Incorrect input '%s' for amount to deposit. Please try again with format 'deposit [amount (non-negative double)]'.\n", (cmdToken+1));
			}
		} else if (strcmp(tempBuffer, "withdraw") == 0 && isServing == 1 && cmdToken != NULL) {
			// checks for correct formatting of amount
			int amountCheck = isProperAmount((cmdToken+1));	
			if (amountCheck == 0)
			{	
				cmd = withdraw;
				isValidCmd = 1;
			} else {
				fprintf(fp, "ERROR: Incorrect input '%s' for amount to withdraw. Please try again with format 'withdraw [amount (non-negative double)]'.\n", (cmdToken+1));
			}
		} else if (strcmp(tempBuffer, "query") == 0 && isServing == 1) {
			cmd = query;
			isValidCmd = 1;
		} else if (strcmp(tempBuffer, "end") == 0) {
			if (isServing == 0) 
			{
				fprintf(fp, "ERROR: No account is currently being served.\n");
			} else {
			cmd = end;
			fprintf(fp, "Stopped Serving Account '%s'.\n", servingAccount);
			isValidCmd = 1;
			isServing = 0;
			}
		} else if (strcmp(tempBuffer, "quit") == 0) {
			cmd = quit;
			isValidCmd = 1;
		} else if (accLen > 256) {
			fprintf(fp, "ERROR: Account name is too long. Account name must be of length 255 characters not including the null terminator.\n");
		} else if (isServing == 1 && (strcmp(tempBuffer, "serve") == 0 || strcmp(tempBuffer, "create") == 0) ) {
			fprintf(fp, "ERROR: Cannot create or serve account '%s' while account '%s' is already being served.\n", (cmdToken+1), servingAccount);
		} else if (isServing == 0 && (strcmp(tempBuffer, "query") == 0 || strcmp(tempBuffer, "deposit") == 0 || strcmp(tempBuffer, "withdraw") == 0)) {
			fprintf(fp, "ERROR: Connot perform actions ['query', 'deposit', 'withdraw'] when no account is being served.\n");
		} else {
			fprintf(fp, "ERROR: The command '%s' is not a valid command. May need more arguments or different command.\n", tempBuffer);
		}
				
		/*
		*	- valid command: client sends it to server and sleeps 
		* 	for 2 seconds to throttle for server
		*	
		*	- non-valid command: client quickly re-prompts for another command
		*/  
		if (isValidCmd == 1)
		{
			// fixes buffer to send enum rather than command
			if (cmdToken != NULL)
			{
				/*
 				 *	Message to server
 				 *		- 1st byte is command
 				 *		- byte 2 to '\0' are account name or amount
 				 */
				snprintf(buffer, sizeof(buffer), "%d%s", cmd, (cmdToken+1)); 
			} else {
				/*
 				 *	Message to server
 				 *		- 1st byte is command
 				 */
				snprintf(buffer, sizeof(buffer), "%d", cmd); 

			}
			
			n = write(sockfd, buffer, strlen(buffer)+1);

			if (n < 0)
			{
				fprintf(fperr, "ERROR writing to socket");
			} 
			
			if (readQuit(session) == 1) break;	
			sleep(2);
		}
		

		if (readQuit(session) == 1) break;
		cmdToken = NULL;
		accLen = 0;
	} 

	free(servingAccount);
 	fclose(fp);
	fclose(fperr);
}

/*
 *	Listens for server responses and sends responses to parser
 */
void* listenToServ(void* args)
{
	FILE* fp = fdopen(STDOUT, "w");
	FILE* fperr = fdopen(STDERR, "w");
	
	int size = 512;
	char* buffer = malloc(sizeof(char)*size);
	int nRead; // number of bytes read	
	
	int isQuitting = 0;
	
	void** params = args;
	int sockfd = *((int*)params[0]);
	Account* session = params[1];

	while (1) 
	{
		bzero(buffer, size);

		nRead = 0;
		size = 512;
		int status = 1; 
		// check that all 512 bytes are read
		while (status > 0)
		{
			status = read(sockfd, buffer+nRead, size);	
			nRead += status;
	
			int i;
			int hasNull = 0;
			for (i = nRead; i < nRead+size; i++)
			{
				if (buffer[i] == '\0')
				{
					hasNull = 1;
						
					fprintf(fp, "Server Response: %s\n\n", buffer);
					if ((isQuitting = parseResponse(session, &sockfd, buffer)) != 0)
					{
						writeQuit(session, 1);
						break;
					}

					int left = 511 - i;
					if (buffer[left] != '0')
					{
						int j;
						for (j = left; j < 512; j++)
						{
							buffer[j - left] = buffer[j];
						}
						nRead = 0;
						size = 512;
						break;	
					}
				}

			} 
			
			if (isQuitting != 0) break;
			size -= status;
		}
		
		// checks for broken pipe between client and server
		/*if (status == 0 && nRead == 0)
		{
			fprintf(fp, "\nBroken pipe. Disconnected from server.\n");
			writeQuit(session, 1);
			if (isQuitting == 1) break;
		} else {*/
		
		if (nRead < 0)
		{ 
			fprintf(fperr, "ERROR reading from socket");
		}
	
	//	}
		
		if (isQuitting == -1)
		{
			fprintf(fp, "\nBroken pipe. Disconnected from server. Press 'Enter' to continue quitting client.\n");
		 	break;
		} else if (isQuitting == 1) {
			break;
		}
	}

	free(buffer);
	close(sockfd);
	fclose(fp);
	fclose(fperr);
}

int main(int argc, char** argv)
{

	if (argc > 3 || argc < 3)
	{
		write(STDERR, "ERROR: incorrect number of arguments entered, please follow the format './executable [host name] [port number]'.\n", 113);
		return 0;
	}	

	//signal(SIGPIPE, signal_callback_handler);		

	char* host = argv[1];
	int port = atoi(argv[2]);

	// Shared session data between client threads
	Account session = {0, 0, NULL, PTHREAD_MUTEX_INITIALIZER};
	
	// client socket and session data
	void* clientArgs[4];
        clientArgs[0] = &port;
	clientArgs[1] = host;
	clientArgs[2] = &session;

	conToServ(clientArgs);

	return 0;	
}
