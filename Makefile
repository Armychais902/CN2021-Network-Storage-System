client: client.cpp
	g++ client.cpp -o client
server: server.c
	gcc server.c -o server
clean:
	rm -f client
	rm -f server