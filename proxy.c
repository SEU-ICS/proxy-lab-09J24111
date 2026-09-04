#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct{
    char *url;
    char object[MAX_OBJECT_SIZE];
    int size;
    int lru;
}cache_clock;
typedef struct{
    cache_clock cache[10];
    int read_cnt;
    sem_t mutex;
    sem_t w;
    int time;
}cache_t;
cache_t cache;

void doit(int fd);
void read_requesthdrs(rio_t *rp);
void parse_url(char *url, char *hostname, char* path, char *port,char *request_header);
void *thread(void *vargp);
void cache_init();
int cache_find(char *url,int fd);
void cache_insert(char *url,char *object,int size);
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

    cache_init();

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
    char url_cache[MAXLINE];
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

    strcpy(url_cache, url);
    if(cache_find(url_cache, fd) == 1){
        return;
    }
    read_requesthdrs(&rio);

    parse_url(url, hostname, path, port, request_header);
    severfd = Open_clientfd(hostname, port);

    Rio_writen(severfd, request_header, strlen(request_header));
    char object_buf[MAX_OBJECT_SIZE];
    int object_size = 0;

    Rio_readinitb(&rio, severfd);
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0){
        Rio_writen(fd, buf, n);
        if(object_size + n < MAX_OBJECT_SIZE){
            memcpy(object_buf + object_size, buf, n);     
        }
        object_size += n;
    }
    close(severfd);

    if(object_size < MAX_OBJECT_SIZE){
        cache_insert(url_cache, object_buf, object_size);
    }
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

void cache_init(){
    cache.read_cnt = 0;
    cache.time = 0;
    Sem_init(&cache.mutex, 0, 1);
    Sem_init(&cache.w, 0, 1);
    for(int i = 0; i < 10; i++){
        cache.cache[i].url = NULL;
        cache.cache[i].size = 0;
        cache.cache[i].lru = 0;
    }
}
int cache_find(char *url,int fd){
    P(&cache.mutex);
    cache.read_cnt++;
    if(cache.read_cnt == 1) P(&cache.w);
    V(&cache.mutex);

    for(int i = 0; i < 10; i++){
        if(cache.cache[i].url != NULL && strcmp(cache.cache[i].url, url) == 0){
            Rio_writen(fd, cache.cache[i].object, cache.cache[i].size);
            cache.cache[i].lru = ++cache.time;
            P(&cache.mutex);
            cache.read_cnt--;
            if(cache.read_cnt == 0) V(&cache.w);
            V(&cache.mutex);
            return 1;
        }
    }

    P(&cache.mutex);
    cache.read_cnt--;
    if(cache.read_cnt == 0) V(&cache.w);
    V(&cache.mutex);
    return 0;
}
void cache_insert(char *url,char *object,int size){
    P(&cache.w);
    int min_lru = cache.cache[0].lru;
    int min_index = 0;
    for(int i = 0; i < 10; i++){
        if(cache.cache[i].url == NULL){
            cache.cache[i].url = Malloc(strlen(url) + 1);
            strcpy(cache.cache[i].url, url);
            memcpy(cache.cache[i].object, object, size);
            cache.cache[i].size = size;
            cache.cache[i].lru = ++cache.time;
            V(&cache.w);
            return;
        }
        if(cache.cache[i].lru < min_lru){
            min_lru = cache.cache[i].lru;
            min_index = i;
        }
    }
    free(cache.cache[min_index].url);
    cache.cache[min_index].url = Malloc(strlen(url) + 1);
    strcpy(cache.cache[min_index].url, url);
    memcpy(cache.cache[min_index].object, object, size);
    cache.cache[min_index].size = size;
    cache.cache[min_index].lru = ++cache.time;
    V(&cache.w);
}