OBJ = bankingServer bankingClient

all:
	gcc -pthread -o bankingClient bankingClient.c
	gcc -pthread -o bankingServer bankingServer.c

refresh: 
	make clean
	make

clean: 
	rm $(OBJ)
