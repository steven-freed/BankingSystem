# Multi-Threaded Banking Client Server
This multi-threaded banking system uses a client and server with C sockets to preform different tasks on a user account.

## Client
Parses user commands to check if they are correct before being sent to the server. Parses responses from the server.

Response: 1st byte is the command, consecutive bytes are account name or amount data

## Server
Accepts the following commands:
```
create [account name] - creates a new account 
serve [account name] - serves an existing account 
deposit [amount] - deposits an amount to the account being served
withdraw [amount] - withdraws amount from account being served, if funds are available
query - prints user's balance
end - ends the session with the account being served
quit - quits the client/server connection
```
account name max length is 255 bytes.
