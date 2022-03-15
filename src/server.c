/* 
References:
- https://wenchiching.wordpress.com/2009/10/14/linux-c-socketclientserver-transfer-file%E5%82%B3%E9%80%81%E6%AA%94%E6%A1%88/
- https://beej-zhtw-gitbook.netdpi.net/client-server_ji_chu
- SP2020 HW1 TA's sample code
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <dirent.h>

#define BUF_SIZE 256

typedef struct
{
    char hostname[BUF_SIZE];
    unsigned short port;
    int listen_fd;
} Server;

typedef struct
{
    int sockfd;
    FILE *fp;
    off_t file_len;
    off_t cnt_len;
    char hostname[BUF_SIZE];
    char in_buf[BUF_SIZE];
    char out_buf[BUF_SIZE];
    char username[BUF_SIZE];
    bool wait_for_write;
    int num_entries;
    int cnt_entries;
    struct dirent **filelist;
    enum
    {
        username,
        request,
        getfile,
        putfile,
        entries,
        getlen,
        putlen,
        get,
        put,
        ls,
        finishmsg,
        finishres
    } state;
} Client;

Server server;
int maxfd; // maximum "number" of fds
Client *clients = NULL;
fd_set master, readfds, writefds;

void SetNonBlockSocket(int socketfd)
{
    int flags;
    if ((flags = fcntl(socketfd, F_GETFL, 0)) < 0)
    {
        perror("fcntl(F_GETFL) failed");
        exit(1);
    }
    if (fcntl(socketfd, F_SETFL, flags | O_NONBLOCK) < 0)
    {
        perror("fcntl(F_SETFL) failed");
        exit(1);
    }
}

static void InitBasic(Client *client)
{
    client->sockfd = -1;
    client->fp = NULL;
    client->file_len = 0;
    client->cnt_len = 0;
    client->state = username;
    client->wait_for_write = false;
    memset(client->in_buf, '\0', BUF_SIZE);
    memset(client->out_buf, '\0', BUF_SIZE);
    memset(client->hostname, '\0', sizeof(client->hostname));
    memset(client->username, '\0', sizeof(client->username));
    client->cnt_entries = 0;
}

static void InitClient(Client *client)
{
    InitBasic(client);
    client->filelist = NULL;
    client->num_entries = 0;
}

static void InitServer(unsigned short port)
{
    struct sockaddr_in server_addr;
    gethostname(server.hostname, sizeof(server.hostname));
    server.port = port;
    if ((server.listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket");
        exit(1);
    }
    SetNonBlockSocket(server.listen_fd);
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    // avoid "address already in use"
    int tmp = 1;
    if (setsockopt(server.listen_fd, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(int)) < 0)
    {
        perror("setsockopt");
        exit(1);
    }
    if (bind(server.listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        exit(1);
    }
    if (listen(server.listen_fd, 1024) == -1)
    {
        perror("listen");
        exit(1);
    }

    // get fd table size and init clients
    maxfd = getdtablesize();
    clients = (Client *)malloc(sizeof(Client) * maxfd);
    if (clients == NULL)
    {
        fprintf(stderr, "%s\n", "malloc error for alloc maximum client");
        exit(1);
    }
    for (int i = 0; i < maxfd; i++)
        InitClient(&clients[i]);

    // set client for listen socket
    clients[server.listen_fd].sockfd = server.listen_fd;
    strcpy(clients[server.listen_fd].hostname, server.hostname);
}

static void FreeClient(Client *client)
{
    InitBasic(client);
    if (client->filelist != NULL)
    {
        for (int i = 0; i < client->num_entries + 2; i++)
            free(client->filelist[i]);
        free(client->filelist);
        client->filelist = NULL;
    }
    client->num_entries = 0;
}

int HandleRead(Client *client)
{
    int nbytes = 0;
    if ((nbytes = recv(client->sockfd, client->in_buf, BUF_SIZE, 0)) <= 0)
    {
        if (nbytes < 0)
            perror("recv error");
    }
    return nbytes;
}

void CloseandFree(Client *client)
{
    FD_CLR(client->sockfd, &master);
    if (FD_ISSET(client->sockfd, &writefds))
        FD_CLR(client->sockfd, &writefds);
    close(client->sockfd);
    FreeClient(client);
}

bool ValidUsername(Client *client)
{
    fprintf(stderr, "check for valid username\n");
    int buf_len = strlen(client->username);
    if (buf_len > 8)
        return false;

    int i;
    for (i = 0; i < maxfd; i++)
    {
        // if (i < 10){
        //     fprintf(stderr, "connection: fd %d; username %s\n", clients[i].sockfd, clients[i].username);
        // }
        if (clients[i].sockfd != -1 && i != client->sockfd && strcmp(clients[i].username, client->username) == 0)
        {
            fprintf(stderr, "check for same name\n");
            int error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(clients[i].sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
            {
                fprintf(stderr, "same name not in use\n");
                CloseandFree(&clients[i]);
                continue;
            }
            else
            {
                fprintf(stderr, "same name in use\n");
                return false;
            }
        }
    }
    return true;
}

bool FindFile(Client *client)
{
    // find if file exist, and store fp
    DIR *dptr;
    if ((dptr = opendir("./server_folder")) == NULL)
    {
        perror("find file");
        return false;
    }
    struct dirent *dir;
    bool find = false;
    char filename[BUF_SIZE];
    while ((dir = readdir(dptr)) != NULL)
    {
        if (strcmp(client->in_buf, dir->d_name) == 0)
        {
            find = true;
            strcpy(filename, dir->d_name);
            break;
        }
    }
    closedir(dptr);

    if (find)
    {
        char filepath[BUF_SIZE];
        memset(filepath, '\0', BUF_SIZE);
        strcpy(filepath, "./server_folder/");
        strcat(filepath, filename);
        fprintf(stderr, "find file: %s\n", filepath);
        client->fp = fopen(filepath, "rb");
        // length of file
        struct stat st;
        stat(filepath, &st);
        client->file_len = st.st_size;
    }
    return find;
}

bool OpenFile(Client *client)
{
    char filepath[BUF_SIZE];
    memset(filepath, '\0', BUF_SIZE);
    strcpy(filepath, "./server_folder/");
    strcat(filepath, client->in_buf);
    if ((client->fp = fopen(filepath, "wb")) != NULL)
        fprintf(stderr, "open put file: %s\n", filepath);
    else
    {
        perror("fopen error");
        return false;
    }
    return true;
}

bool DirectoryEntries(Client *client)
{
    int n = scandir("./server_folder", &client->filelist, NULL, alphasort);
    if (n < 0)
    {
        perror("scandir");
        return false;
    }
    client->num_entries = n - 2;
    return true;
}

void InputProcessing(Client *client, int nread)
{
    if (client->state == username)
    {
        memset(client->username, '\0', BUF_SIZE);
        strcpy(client->username, client->in_buf);
        memset(client->in_buf, '\0', BUF_SIZE);
        fprintf(stderr, "client username: %s\n", client->username);
        if (ValidUsername(client))
        {
            fprintf(stderr, "client username valid\n");
            char *ans = "valid\n";
            strcpy(client->out_buf, ans);
            client->wait_for_write = true;
        }
        else
        {
            fprintf(stderr, "client username invalid\n");
            char *ans = "invalid\n";
            strcpy(client->out_buf, ans);
            client->wait_for_write = true;
        }
    }
    else if (client->state == request)
    {
        fprintf(stderr, "current state: request\n");
        if (strcmp(client->in_buf, "get") == 0 || strcmp(client->in_buf, "put") == 0)
        {
            client->state = (client->in_buf[0] == 'g') ? getfile : putfile;
        }
        else if (strcmp(client->in_buf, "ls") == 0)
        {
            client->state = entries;
            // store list of file entries and number of entries
            if (DirectoryEntries(client))
                sprintf(client->out_buf, "%d", client->num_entries); // num_entries exclude ".", ".."
            else
            {
                strcpy(client->out_buf, "ls error"); // "%d" no "\n"
                client->filelist = NULL;
            }
            fprintf(stderr, "server ls result: %s\n", client->out_buf);
        }
        memset(client->in_buf, '\0', BUF_SIZE);
    }
    else if (client->state == getfile)
    {
        fprintf(stderr, "get file name: %s\n", client->out_buf);
        // find if file exist, and store fp
        if (FindFile(client))
            strcpy(client->out_buf, "get file exist\n");
        else
            strcpy(client->out_buf, "get not exist\n");
        fprintf(stderr, "server find file result\n");
        client->wait_for_write = true;
        memset(client->in_buf, '\0', BUF_SIZE);
    }
    else if (client->state == putfile)
    {
        // open file, and store fp
        fprintf(stderr, "put file name: %s\n", client->out_buf);
        if (OpenFile(client))
            strcpy(client->out_buf, "put open exist\n");
        else
            strcpy(client->out_buf, "put open failed\n");
        client->wait_for_write = true;
        memset(client->in_buf, '\0', BUF_SIZE);
    }
    else if (client->state == putlen)
    {
        client->file_len = atoi(client->in_buf);
        fprintf(stderr, "negotiate the lenth of put file %ld\n", client->file_len);
        memset(client->in_buf, '\0', BUF_SIZE);
        client->state = put;
    }
    else if (client->state == put)
    {
        int nbytes = fwrite(client->in_buf, sizeof(char), nread, client->fp);
        memset(client->in_buf, '\0', BUF_SIZE);
        client->cnt_len += nbytes;
        if (client->cnt_len == client->file_len)
        {
            client->state = finishres;
            strcpy(client->out_buf, "put file finished\n");
        }
    }
    else if (finishmsg)
    {
        if (strcmp(client->in_buf, "get file finished\n") == 0)
        {
            client->state = request;
            memset(client->in_buf, '\0', BUF_SIZE);
            fclose(client->fp);
            client->file_len = 0;
            client->cnt_len = 0;
        }
        else if (strcmp(client->in_buf, "ls finished\n") == 0)
        {
            client->state = request;
            memset(client->in_buf, '\0', BUF_SIZE);
            client->cnt_entries = 0;
            if (client->filelist != NULL)
            {
                for (int i = 0; i < client->num_entries + 2; i++)
                    free(client->filelist[i]);
                free(client->filelist);
                client->filelist = NULL;
            }
            client->num_entries = 0;
        }
    }
}

void OutputProcessing(Client *client)
{
    if (client->wait_for_write && client->state == username)
    {
        fprintf(stderr, "username result before send: %s", client->out_buf);
        if (send(client->sockfd, client->out_buf, BUF_SIZE, MSG_NOSIGNAL) < 0)
            CloseandFree(client);
        else
        {
            fprintf(stderr, "username result send successfully\n");
            client->wait_for_write = false;
            client->state = (strcmp(client->out_buf, "valid\n") == 0) ? request : username;
            memset(client->out_buf, '\0', BUF_SIZE);
        }
    }
    else if (client->wait_for_write && (client->state == getfile || client->state == putfile))
    {
        if (send(client->sockfd, client->out_buf, BUF_SIZE, MSG_NOSIGNAL) < 0)
            CloseandFree(client);
        else
        {
            fprintf(stderr, "server send getfile/putfile response successfully: %s", client->out_buf);
            client->wait_for_write = false;
            if (strcmp(client->out_buf, "get file exist\n") == 0)
                client->state = getlen;
            else if (strcmp(client->out_buf, "get not exist\n") == 0)
                client->state = request;
            else if (strcmp(client->out_buf, "put open failed\n") == 0)
                client->state = request;
            else if (strcmp(client->out_buf, "put open exist\n") == 0)
                client->state = putlen;
            memset(client->out_buf, '\0', BUF_SIZE);
        }
    }
    else if (client->state == entries)
    {
        if (send(client->sockfd, client->out_buf, BUF_SIZE, MSG_NOSIGNAL) < 0)
            CloseandFree(client);
        else
        {
            memset(client->out_buf, '\0', BUF_SIZE);
            client->state = ls;
        }
    }
    else if (client->state == getlen)
    {
        // send the length of file to server
        sprintf(client->out_buf, "%ld", client->file_len);
        if (send(client->sockfd, client->out_buf, BUF_SIZE, MSG_NOSIGNAL) < 0)
            CloseandFree(client);
        else
        {
            memset(client->out_buf, '\0', BUF_SIZE);
            client->state = get;
        }
    }
    else if (client->state == get)
    {
        int nread = fread(client->out_buf, sizeof(char), BUF_SIZE, client->fp);
        if (send(client->sockfd, client->out_buf, nread, MSG_NOSIGNAL) < 0)
            CloseandFree(client);
        else
        {
            memset(client->out_buf, '\0', BUF_SIZE);
            client->cnt_len += nread;
            if (client->cnt_len == client->file_len)
                client->state = finishmsg;
        }
    }
    else if (client->state == ls)
    {
        if (client->cnt_entries < client->num_entries) // if empty folder
        {
            sprintf(client->out_buf, "%s", client->filelist[client->cnt_entries + 2]->d_name);
            if (send(client->sockfd, client->out_buf, BUF_SIZE, MSG_NOSIGNAL) < 0)
                CloseandFree(client);
            else
            {
                memset(client->out_buf, '\0', BUF_SIZE);
                client->cnt_entries++;
                if (client->cnt_entries == client->num_entries)
                    client->state = finishmsg;
            }
        }
    }
    else if (client->state == finishres)
    {
        fprintf(stderr, "command finished: %s", client->out_buf);
        if (send(client->sockfd, client->out_buf, BUF_SIZE, MSG_NOSIGNAL) < 0)
            CloseandFree(client);
        else
        {
            client->state = request;
            memset(client->out_buf, '\0', BUF_SIZE);
            fclose(client->fp);
            client->file_len = 0;
            client->cnt_len = 0;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "%s\n", "Command line arguments error.");
        exit(1);
    }

    InitServer((unsigned short)atoi(argv[1]));
    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", server.hostname, server.port, server.listen_fd, maxfd);

    // create folder
    int status;
    if ((status = mkdir("./server_folder", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) < 0)
    {
        perror("create folder error");
        if (errno != EEXIST)
            exit(1);
    }

    int conn_fd;
    FD_ZERO(&master);
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_SET(server.listen_fd, &master);
    fprintf(stderr, "listen_fd %d\n", server.listen_fd);

    struct sockaddr_in client_addr; // client address
    socklen_t addrlen;

    // A file descriptor is considered ready if it is possible to perform the corresponding I/O operation (e.g., read(2)) without blocking.
    // recv can still work after client connection close (normally)
    while (true)
    {
        readfds = master;
        writefds = master;
        // nfds of select should be set to the highest-numbered file descriptor in any of the three sets, plus 1
        if (select(maxfd, &readfds, &writefds, NULL, NULL) == -1)
        {
            if (errno == EINTR)
                continue;
            perror("select error");
            exit(1);
        }

        // handle new connection
        if (FD_ISSET(server.listen_fd, &readfds))
        {
            addrlen = sizeof(client_addr);
            // if connection lost before accept: set server socket as O_NONBLOCK
            if ((conn_fd = accept(server.listen_fd, (struct sockaddr *)&client_addr, &addrlen)) == -1)
            {
                if (errno == EINTR || errno == EAGAIN)
                    continue; // try again
                perror("accept error");
                exit(1);
            }
            else
            {
                clients[conn_fd].sockfd = conn_fd;
                strcpy(clients[conn_fd].hostname, inet_ntoa(client_addr.sin_addr));
                fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, clients[conn_fd].hostname);
                FD_SET(conn_fd, &master);
            }
        }
        // if connection close, "ready" but recv return 0
        for (int i = server.listen_fd + 1; i < maxfd; i++)
        {
            if (FD_ISSET(i, &readfds))
            {
                // process client request
                // nbytes == 0: connection closed
                int ret = HandleRead(&clients[i]);
                if (ret <= 0)
                    CloseandFree(&clients[i]);
                else
                {
                    InputProcessing(&clients[i], ret);
                }
            } // read FD_ISSET
            if (FD_ISSET(i, &writefds))
            {
                OutputProcessing(&clients[i]);
            } //write FD_ISSET
        }     // loop all connection
    }         // select loop
    close(server.listen_fd);
    return 0;
}