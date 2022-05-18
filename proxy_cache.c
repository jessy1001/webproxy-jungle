#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestline_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *host_key = "Host";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *user_agent_key = "User-Agent";

/* end server info for testing */
// static const char *end_server_host = "localhost";   // 현재, 한 컴퓨터에서 프록시와, 서버가 돌아가기 때문에 localhost라고 지칭
static const int end_server_port = 8000;           // tiny server의 포트 번호

void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void parse_uri(char *uri, char *host, int *port, char *path);
void build_http_header(char *http_header, char *hostname, char *path, rio_t *client_rio);
int connect_endServer(char *hostname, int port);
/* cache할 때 추가 */
void *thread(void *vargsp); 
void init_cache(void);
int reader(int connfd, char *url);
void writer(char *url, char *buf);

typedef struct{
  char *url;    //url 담을 변수
  int *flag;    //캐시가 비어있는지 여부 
  int *cnt;     //해당 캐시 최근에 방문한지 얼마나 되었는지
  char *content;//클라이언트에 보낼 내용
} Cache_info;

Cache_info *cache; //cache 변수 선언
int readcnt;       //cache reader() 중인 스레드의 개수
sem_t mutex, w;

/* Thread Routine*/
void *thread(void *vargp){
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp); //
  doit(connfd);
  Close(connfd); //
  return NULL;
}

int main(int argc, char **argv) {
   /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    init_cache(); /**/
    int listenfd, connfd;             //listen to connection
    int *connfdp;
    socklen_t clientlen;
    char clienthost[MAXLINE], clientport[MAXLINE];
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    listenfd = Open_listenfd(argv[1]);  // 지정한 포트 번호로 듣기 식별자 생성
    
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA*) &clientaddr, &clientlen);
        
        Getnameinfo((SA *)&clientaddr, clientlen, clienthost, MAXLINE, clientport, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", clienthost, clientport);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
}

/** 한 개의 HTTP transaction 처리 */
void doit(int connfd) { 
    int endserver_fd;
    char content_buf[MAX_OBJECT_SIZE]; /*cache숙제에서 추가*/
    char url[MAXLINE]; /*cache숙제에서 추가*/
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char endserver_http_header[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    int port;
    rio_t rio, endserver_rio;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);            //connfd로 들어온 request line 읽기
    sscanf(buf, "%s %s %s", method, uri, version);
    strcpy(url, uri); /*cache숙제에서추가*/

    if (strcasecmp(method, "GET")) {   //method이 GET이 아닐 경우
        clienterror(connfd, method, "501", "Not implemented", "Proxy does not implement this method");
        return;
    }

    /* cache hit */
    if(reader(connfd, url)){
      return;
    }

    parse_uri(uri, hostname, &port, path); // connfd로 들어온 request line의 uri를 parse 하기
    // build http request for end server
    build_http_header(endserver_http_header, hostname, path, &rio);    //hostname, path, rio가지고 endserver_http_header에 필요한 헤더 다 모아놓음
    // connect to end server
    endserver_fd = connect_endServer(hostname, port);                       //요청받았던 request line의 hostname, port로 end server에 connect 요청
    if (endserver_fd < 0) {
      printf("connection failed\n");
      return;
    }

    Rio_readinitb(&endserver_rio, endserver_fd);                                      //ptr to internal buffer, fd it will be copying from
    Rio_writen(endserver_fd, endserver_http_header, strlen(endserver_http_header));   // end server에 요청(각종 헤더) 전달
    
    size_t n;
    int total_size = 0; /*cache에 담을 바이트 수*/
    while ((n = Rio_readlineb(&endserver_rio, buf, MAXLINE)) != 0) {          // endserver_fd -> endserver_rio -> buf (endserver_fd에서 한 칸씩 한 줄동안 읽어와서 buf에 넣어줘)
        printf("Proxy received %ld bytes, then send\n", n);                   // proxy에 end server에서 받은 문자수를 출력하고
        Rio_writen(connfd, buf, n);                                               // buf -> fd   //client에 end server response를 출력
        /* cache content의 최대 크기를 넘지 않으면 content_buf에 담음 */
        if (total_size + n < MAX_OBJECT_SIZE){
          strcpy(content_buf + total_size, buf);
        }
        total_size += n;
    }

    /* cache content의 최대 크기를 넘지 않았다면 cache에 쓰기*/
    if(total_size < MAX_OBJECT_SIZE){
      writer(url, content_buf);
    }

    Close(endserver_fd);
    return;
}

void parse_uri(char *uri, char *host, int *port, char *path) {
    // *port = end_server_port; 
    char *pos = strstr(uri, "//"); // uri에 '//'가 있다면 자르기 위해 해당 위치를 pos에 저장함
    pos = pos != NULL? pos + 2 : uri; // pos가 NULL이 아닐 경우, pos+2로 위치를 잡아주고, NULL경우에는 uri를 그대로 사용

    char *pos2 = strstr(pos, ":"); // port번호를 분리하기 위해 ':'의 위치를 저장
    if (pos2 != NULL) {                      // port 번호 지정되어 있는 경우
      *pos2 = '\0'; // ':'을 '\0'으로 변환 (문자열의 끝을 의미)
      sscanf(pos, "%s", host); // host는 cut의 앞부분
      sscanf(pos2 + 1, "%d%s", port, path);        // ':' 건너뛰기
    } else {
      pos2 = strstr(pos, "/");
      if (pos2 != NULL) {                    // path가 있는 경우
        *pos2 = '\0';
        sscanf(pos, "%s", host);
        *pos2 = '/';
        sscanf(pos2, "%s", path);
      } else {                              // host만 있는 경우
        sscanf(pos, "%s", host);
      }
    }
    // if (strlen(host) == 0) strcpy(host, end_server_host);   // host명이 없는 경우 지정

    return;
}


void build_http_header(char *http_header, char *hostname, char *path, rio_t *client_rio) {
  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
  
  // request line
  sprintf(request_hdr, requestline_hdr_format, path);

  // get other request header for client rio and change it
  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
    if (strcmp(buf, endof_hdr) == 0)
      break;  // EOF
    
    if (!strncasecmp(buf, host_key, strlen(host_key))) {
      strcpy(host_hdr, buf);
      continue;
    }

    if (strncasecmp(buf, connection_key, strlen(connection_key))
        &&strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
        &&strncasecmp(buf, user_agent_key, strlen(user_agent_key))) {
        strcat(other_hdr, buf);
      }
  }
  if (strlen(host_hdr) == 0) {
    sprintf(host_hdr, host_hdr_format, hostname);
  }
  sprintf(http_header, "%s%s%s%s%s%s%s",
          request_hdr,
          host_hdr,
          conn_hdr,
          prox_hdr,
          user_agent_hdr,
          other_hdr,
          endof_hdr);
  return;
}

/** 에러 메세지를 클라이언트에 보냄 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy server</em>\r\n", body);

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

inline int connect_endServer(char *hostname, int port) {
  char portStr[100];
  sprintf(portStr, "%d", port);  //int -> char*
  return Open_clientfd(hostname, portStr);
}

/*cache 초기화 */
void init_cache(){
    Sem_init(&mutex, 0, 1);               //initialize unnamed semaphore at &mutex to 1 (가운데 0: between threads)
    Sem_init(&w, 0, 1);
    readcnt = 0;
    cache = (Cache_info*)Malloc(sizeof(Cache_info) * 10);   //cache라는, cache struct 가리키는 포인터
    for (int i = 0; i < 10; i++){
        cache[i].url = (char *)Malloc(sizeof(char) * 256);
        cache[i].flag = (int *)Malloc(sizeof(int));
        cache[i].cnt = (int *)Malloc(sizeof(int));
        cache[i].content = (char *)Malloc(sizeof(char) * MAX_OBJECT_SIZE);
        *(cache[i].flag) = 0; //일단 비어있다고 설정
        *(cache[i].cnt) = 1;
    }

}

/*cache에서 요청한 url 있는지 찾기*/
/* 세마포어를 이용해서 reader 먼저 실행 - 여러 스레드가 읽고 있으면 writer는 실행 못함 */
int reader(int connfd, char *url){
  int return_flag = 0; //캐시에서 찾으면 1, 못 찾으면 0
  P(&mutex);//readcnt에 접근(한 번에 readcnt 한 개씩만 올려야 되니까)
  readcnt++; //이걸 올린 애가 readcnt == 1이어야 하니까 &mutex 잠가주는 거임.
  if(readcnt == 1){ //전에 들어갔던 애가 없는 것: 이제 cache read 할 거니까 cache write은 오지마~~!!
    P(&w); //&cache write 이제 못 옴 <!---------------------------------->
  }
  V(&mutex); //

  /*cache를 다 돌면서 cache에 써있고 cache의 url과 현재 요청한 url이 같으면client fd에 cache의 내용 쓰고 해당 cache의 cnt(최근 방문 순서)를 0으로 초기화 후 break */
  for(int i = 0; i < 10; i++){
    if(*(cache[i].flag) == 1 && !strcmp(cache[i].url, url)){
      Rio_writen(connfd, cache[i].content, MAX_OBJECT_SIZE);
      return_flag = 1;
      *(cache[i].cnt) = 1; //최근 방문 순서
      break;
    }
  }

  /* 모든 cache객체의 cnt를 하나씩 올려줌(즉, 방문 안 한 일수) -> 그럼 방문 한 거는??*/
  for (int i = 0; i < 10; i++){
    (*(cache[i].cnt))++;
  }
  P(&mutex);  //mutex에 wait(try)
  readcnt--;
  if(readcnt == 0){ //마지막 나오는 애가 문 닫기(아래)
    V(&w); //&cache write 이제 와도 돼 <!---------------------------------->
  }
  V(&mutex);
  return return_flag;

}

/* cache에 쓰기 */
void writer(char *url, char *buf){
  P(&w); //cache에 write 못하게 잠가

  int idx = 0; //작성할 곳을 가리키는 index
  int max_cnt = 0;

  for(int i = 0; i < 10; i++){
    if(*(cache[i].flag) == 0){              //해당 cache index is empty
      idx = i;
      break;
    }
    if(*(cache[i].cnt) > max_cnt){
      idx = i;
      max_cnt = *(cache[i].cnt);            //제일 오랫동안 안 찾은 일수 업데이트
    }

  }
  /* 해당 index에 cache 작성 */
  *(cache[idx].flag) = 1; //이제 이 캐시는 찼다
  strcpy(cache[idx].url, url);
  strcpy(cache[idx].content, buf);
  *(cache[idx].cnt) = 1;

  V(&w); //이제 다른 thread들 you may write on cache

}