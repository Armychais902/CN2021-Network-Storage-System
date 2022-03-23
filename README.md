# CN2021: Network Storage System
Computer Networks, Fall 2021, Project Phase 1

## Introduction
Implement a single Network Storage System:
- Client can GET/PUT file to server, and LIST files in server
- All the transmission is done by TCP sockets
- I/O multiplexing to serve multiple client at the same time

## How to Run
- First, generate `client` and `server` binary
```
$ make client
$ make server
```
- And launch them:
```
# Specify the port number 
$ ./server [port]
# Specify the IP of server and the port server provide
$ ./client [ip:port]
```
- After launch, you'll see `client_folder` and `server_folder` are generated
- When client build up the connection, its username for login will be checked to see if the name is in active use:
```
$ ./client 127.0.0.1:2345
input your username:
Armychais
username is in used, please try another:
Armychais902
connect successfully
```
- After successfully login, client can enter command `get`, `put`, and `ls`:
```
$ ls # Print all files store in server, sorted in lexicographical order
$ put <filename> # Upload the file with the <filename> to server
$ get <filename> # Download the file with the <filename> to client
```

## Report
Please refer to [report.pdf](https://github.com/Armychais902/CN2021-Network-Storage-System/blob/main/report.pdf):
- Draw the flowchart of building connection and file transferring
- Explanation and handle of `SIGPIPE`
