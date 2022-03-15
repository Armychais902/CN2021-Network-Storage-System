#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <string>
#include <cstring>
#include <sstream>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>

using namespace std;

#define BUF_SIZE 256

int sockfd;

void HandleWrite(const char *str, int len)
{
    if (send(sockfd, str, len, MSG_NOSIGNAL) < 0)
    {
        perror("send");
        exit(1);
    }
}

// nread can be NULL
void HandleRead(char *str, int *nread)
{
    int nbytes = 0;
    if ((nbytes = recv(sockfd, str, BUF_SIZE, 0)) <= 0)
    {
        if (nbytes < 0)
        {
            perror("recv error");
            exit(1);
        }
        else
        {
            perror("recv connection close");
            exit(1);
        }
    }
    if (nread != NULL)
        *nread = nbytes;
}

void InputUsername()
{
    printf("input your username:\n");

    string username;
    while (true)
    {
        cin >> username;
        HandleWrite(username.c_str(), BUF_SIZE);

        char ans[BUF_SIZE];
        memset(ans, '\0', BUF_SIZE);
        HandleRead(ans, NULL);
        fprintf(stderr, "username result: %s\n", ans);
        if (strcmp(ans, "valid\n") == 0)
        {
            printf("connect successfully\n");
            break;
        }
        else
            printf("username is in used, please try another:\n");
    }
}

bool FindFile(string fname)
{
    DIR *dptr;
    if ((dptr = opendir("./client_folder")) == NULL)
    {
        perror("open client folder");
        exit(1);
    }
    struct dirent *dir;
    bool find = false;
    while ((dir = readdir(dptr)) != NULL)
    {
        if (strcmp(fname.c_str(), dir->d_name) == 0)
        {
            find = true;
            break;
        }
    }
    closedir(dptr);

    if (find)
    {
        string fpath = "./client_folder/" + fname;
        cerr << "find file: " << fpath << endl;
    }
    return find;
}

void SendFile(string fname)
{
    string fpath = "./client_folder/" + fname;
    FILE *fp = fopen(fpath.c_str(), "rb");
    char buf[BUF_SIZE];

    // send the length of file to server
    struct stat st;
    stat(fpath.c_str(), &st);
    int size = st.st_size;
    string len = to_string(size);
    HandleWrite(len.c_str(), BUF_SIZE);
    while (!feof(fp))
    {
        memset(buf, '\0', BUF_SIZE);
        int nread = fread(buf, sizeof(char), BUF_SIZE, fp);
        HandleWrite(buf, nread);
    }
    // recv to see if server is finished
    char ans[BUF_SIZE];
    memset(ans, '\0', BUF_SIZE);
    HandleRead(ans, NULL);
    if (strcmp(ans, "put file finished\n") == 0)
        cout << "put " << fname << " successfully\n";
}

void ReceiveFile(string fname)
{
    string fpath = "./client_folder/" + fname;
    FILE *fp;
    if ((fp = fopen(fpath.c_str(), "wb")) == NULL)
    {
        perror("fopen error");
        exit(1);
    }

    // recv length of file
    char ans[BUF_SIZE];
    memset(ans, '\0', BUF_SIZE);
    HandleRead(ans, NULL);
    int len = atoi(ans);
    fprintf(stderr, "receive file length %d\n", len);

    int bytescnt = 0;
    char buf[BUF_SIZE];
    while (true && bytescnt < len)
    {
        int nread = 0;
        memset(buf, '\0', BUF_SIZE);
        HandleRead(buf, &nread);
        bytescnt += nread;
        fwrite(buf, sizeof(char), nread, fp);
    }
    fclose(fp);
    // send finish to server
    string endmsg = "get " + fname + " successfully\n";
    HandleWrite("get file finished\n", BUF_SIZE);
    cout << endmsg;
}

void ListDirectory(int entries)
{
    int cnt_entries = 0;
    while (true && cnt_entries < entries)
    {
        char buf[BUF_SIZE];
        HandleRead(buf, NULL);
        cnt_entries++;
        printf("%s\n", buf);
    }
    // send finish to server
    HandleWrite("ls finished\n", BUF_SIZE);
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "%s\n", "Command line arguments error.");
        exit(1);
    }
    char *token = strtok(argv[1], ":");
    char addr[BUF_SIZE];
    strncpy(addr, token, strlen(token));
    addr[strlen(token)] = '\0';
    token = strtok(NULL, ":");
    int port = atoi(token);
    fprintf(stderr, "addr: %s, port: %d\n", addr, port);

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        exit(1);
    }
    struct sockaddr_in local_addr;
    bzero(&local_addr, sizeof(local_addr));
    local_addr.sin_family = PF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = inet_addr(addr);

    if (connect(sockfd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
    {
        perror("connect");
        exit(1);
    }

    int status;
    if ((status = mkdir("./client_folder", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)) < 0)
    {
        perror("create folder error");
        if (errno != EEXIST)
            exit(1);
    }

    InputUsername();

    string tmp;
    getline(cin, tmp);
    while (true)
    {
        string input, split;
        getline(cin, input);
        stringstream input_stream(input);
        int cnt = 0;
        string command, filename;
        cerr << "read command input: ";
        while (getline(input_stream, split, ' '))
        {
            if (cnt == 0)
                command = split;
            if (cnt == 1)
                filename = split;
            if (cnt == 3)
                break;
            cnt++;
            cerr << split << " ";
        }
        cerr << endl;

        if (command == "put")
        {
            if (cnt > 2)
            {
                cout << "Command format error" << endl;
                continue;
            }
            if (FindFile(filename))
            {
                HandleWrite(command.c_str(), BUF_SIZE);
                HandleWrite(filename.c_str(), BUF_SIZE);
                // recv to see if server is ready
                char ans[BUF_SIZE];
                memset(ans, '\0', BUF_SIZE);
                HandleRead(ans, NULL);
                if (strcmp(ans, "put open exist\n") == 0)
                    SendFile(filename);
                else
                    cerr << "server can't receive file now" << endl;
            }
            else
                cout << "The " << filename << " doesn’t exist" << endl;
        }
        else if (command == "get")
        {
            if (cnt > 2)
            {
                cout << "Command format error" << endl;
                continue;
            }
            HandleWrite(command.c_str(), BUF_SIZE);
            HandleWrite(filename.c_str(), BUF_SIZE);
            fprintf(stderr, "send command and filename to server\n");
            // recv to see if server is ready
            char ans[BUF_SIZE];
            memset(ans, '\0', BUF_SIZE);
            HandleRead(ans, NULL);
            fprintf(stderr, "read ans from server\n");
            if (strcmp(ans, "get file exist\n") == 0)
                ReceiveFile(filename);
            else
                cout << "The " << filename << " doesn’t exist" << endl;
        }
        else if (command == "ls")
        {
            if (cnt > 1)
            {
                cout << "Command format error" << endl;
                continue;
            }
            HandleWrite(command.c_str(), BUF_SIZE);
            // recv to see how much entries
            char ans[BUF_SIZE];
            memset(ans, '\0', BUF_SIZE);
            HandleRead(ans, NULL);
            if (strcmp(ans, "ls error") == 0)
                cerr << "server can't ls now" << endl;
            else
            {
                int entries = atoi(ans);
                fprintf(stderr, "receive number of entries %d\n", entries);
                ListDirectory(entries);
            }
        }
        else
            cout << "Command not found" << endl;
    }

    return 0;
}