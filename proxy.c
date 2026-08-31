#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);
void read_requesthdrs(rio_t *rp);
void parse_url(char *url, char *hostname, char* path, char *port,char *request_header);
void *thread(void *vargp);
int main(int argc, char **argv)
{
    int listenfd;
    int *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if(argc !=2){
        fprintf(stderr,"Usage: %s <port number>\n", argv[0]);
        exit(1);
    }
    Signal(SIGPIPE, SIG_IGN);
    listenfd = Open_listenfd(argv[1]);
    while(1){
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid,NULL,thread,connfd);
        // doit(connfd);
        // Close(connfd);
    }
    //printf("%s", user_agent_hdr);
    return 0;
}

//handle the request from the client and forward it to the server, then send the response back to the client
void doit(int fd){
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE], request_header[MAXLINE];
    int severfd;
    rio_t rio;
    ssize_t n;

    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, url, version);
    
    if(strcasecmp(method, "GET")){
        printf("Proxy does not implement the method\n");
        return;
    }

    read_requesthdrs(&rio);

    parse_url(url, hostname, path, port, request_header);
    severfd = Open_clientfd(hostname, port);

    Rio_writen(severfd, request_header, strlen(request_header));
    Rio_readinitb(&rio, severfd);
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0){
        Rio_writen(fd, buf, n);
    }
    close(severfd);
}

//handle the url and parse it into hostname, path, port and request header
void parse_url(char *url, char *hostname, char* path, char *port,char *request_header){
    const char *start = url;
    const char *port_start = NULL;
    const char *path_start = NULL;

    start = strstr(url ,"http://");
    if(start == NULL)return;
    start += 7;

    path_start =strstr(start, "/");
    strcpy(path, path_start);

    strncpy(hostname, start, path_start - start);
    hostname[path_start - start] = '\0';

    port_start = strstr(hostname, ":");  
    if(port_start != NULL){
        strncpy(port, port_start + 1, strlen(hostname) - (port_start - hostname) - 1);
        port[strlen(hostname) - (port_start - hostname) - 1] = '\0';
        hostname[port_start - hostname] = '\0';
    }
    else strcpy(port, "80");

    sprintf(request_header, 
            "GET %s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "%s"
            "Connection: close\r\n"
            "Proxy-Connection: close\r\n\r\n", 
            path, hostname, user_agent_hdr);
    
}

void read_requesthdrs(rio_t *rp){
    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")){
        Rio_readlineb(rp, buf, MAXLINE);
    }
}

void *thread(void *vargp){
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}