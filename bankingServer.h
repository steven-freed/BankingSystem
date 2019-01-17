#ifndef LOADFILE_H
#define LOADFILE_H

#define STDIN 0
#define STDOUT 1
#define STDERR 2

typedef struct _AccountEntry {
	char name[255];
	double balance;
	int served;
	pthread_mutex_t servedMutex;
	struct _AccountEntry* next;
} AccountEntry;

typedef struct _AccountsDb {
        AccountEntry* first;
        int length;
        pthread_mutex_t mutex;
} AccountsDb;

AccountsDb* createAccountsDb();
void freeAccountsDb(AccountsDb*);
int createAccountEntry(AccountsDb*, char*);
AccountEntry* getAccountsDb(AccountsDb*, char*, int);
int signinAccountEntry(AccountEntry*);
int signoutAccountEntry(AccountEntry*);

typedef struct _DLLNode {
	struct _DLLNode* prev;
	struct _DLLNode* next;
	int socketFd;
	pthread_t thread;
	AccountEntry* ae;
	pthread_mutex_t workingMutex;
	int working;
} DLLNode;

typedef struct _DoubleLinkedList {
	DLLNode* first;
	DLLNode* last;
	int length;
	pthread_mutex_t changeMutex;
} DLL;

DLL* createDLL();
void freeDLL(DLL*);
DLLNode* createDLLNode();
void freeDLLNode(DLLNode*);
int addDLL(DLL*, DLLNode*);
int removeDLL(DLL*, DLLNode*, int);

#endif
