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

/* end server info */
static const char *end_server_host = "localhost";   // 현재, 한 컴퓨터에서 프록시와, 서버가 돌아가기 때문에 localhost라고 지칭
static const int end_server_port = 8000;           // tiny server의 포트 번호

void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void parse_uri(char *uri, char *host, int *port, char *path);
void build_http_header(char *http_header, char *hostname, char *path, rio_t *client_rio);
int connect_endServer(char *hostname, int port);

/* Thread Routine*/
// void *thread(void *vargp){
//   int connfd = *((int *)vargp);
//   Pthread_detach(pthread_self());
//   Free(vargp); //
//   doit(connfd);
//   Close(connfd); //
//   return NULL;
// }

int main(int argc, char **argv) {
    int listenfd, connfd;             //listen to connection
    int *connfdp;
    socklen_t clientlen;
    char clienthost[MAXLINE], clientport[MAXLINE];
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);  // 지정한 포트 번호로 듣기 식별자 생성
    
    while (1) {
      clientlen = sizeof(clientaddr);
      connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
      // connfdp = Malloc(sizeof(int));
      // *connfdp = Accept(listenfd, (SA*) &clientaddr, &clientlen);
      
      Getnameinfo((SA *)&clientaddr, clientlen, clienthost, MAXLINE, clientport, MAXLINE, 0);
      printf("Accepted connection from (%s, %s)\n", clienthost, clientport);
      // Pthread_create(&tid, NULL, thread, connfdp);
      doit(connfd);   // line:netp:tiny:doit
      Close(connfd);  // line:netp:tiny:close
    }
    return 0;
}

/** 한 개의 HTTP 트랜잭션을 처리 */
void doit(int connfd) { //browser 요청 받아들이는 connfd임
    int endserver_fd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char endserver_http_header[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    int port;
    rio_t rio, endserver_rio;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);                                 //connfd로 들어온 request line 읽기
    sscanf(buf, "%s %s %s", method, uri, version);
        
    if (strcasecmp(method, "GET")) {                                   //method이 GET이 아닐 경우
        clienterror(connfd, method, "501", "Not implemented", "Proxy does not implement this method");
        return;
    }

    parse_uri(uri, hostname, &port, path);                             // connfd로 들어온 request line의 uri를 parse 하기
    build_http_header(endserver_http_header, hostname, path, &rio);    //buid http request for end server //hostname, path, rio가지고 endserver_http_header에 필요한 헤더 다 모아놓음
    endserver_fd = connect_endServer(hostname, port);                  //connect to end server //요청받았던 request line의 hostname, port로 end server에 connect 요청
    if (endserver_fd < 0) {
      printf("connection failed\n");
      return;
    }

    Rio_readinitb(&endserver_rio, endserver_fd);                                      //ptr to internal buffer, fd it will be copying from
    Rio_writen(endserver_fd, endserver_http_header, strlen(endserver_http_header));   // end server에 요청(각종 헤더) 전달
    
    size_t n;
    while ((n = Rio_readlineb(&endserver_rio, buf, MAXLINE)) != 0) {          // endserver_fd -> endserver_rio -> buf (endserver_fd에서 한 칸씩 한 줄동안 읽어와서 buf에 넣어줘)
        printf("Proxy received %ld bytes, then send\n", n);                   // proxy에 end server에서 받은 문자수를 출력하고
        Rio_writen(connfd, buf, n);                                           // buf -> connfd   //client에 end server response를 출력
    }

    Close(endserver_fd);
    return;
}

void parse_uri(char *uri, char *host, int *port, char *path) {
    *port = end_server_port; 
    char *pos = strstr(uri, "//");                                  // uri에 '//'가 있다면 해당 위치를 pos에 저장함
    pos = pos != NULL? pos + 2 : uri;                               // '//'가 있는 경우 pos+2로 위치를 잡아주고, NULL경우에는 uri를 그대로 사용

    char *pos2 = strstr(pos, ":");                                  // port번호를 분리하기 위해 ':'의 위치를 저장
    if (pos2 != NULL) {                                        // port 번호가 입력되어있을 경우
      *pos2 = '\0';                                                 // ':'을 '\0'으로 변환해서 
      sscanf(pos, "%s", host);                                      // '//'뒤부터 ':'앞까지(페이지주소=host) 하나의 string으로 host에 저장
      sscanf(pos2 + 1, "%d%s", port, path);                         // ':' 뒤부터 port, path에 복사
    } else {                                                   // port번호가 입력되지 않은 경우
      pos2 = strstr(pos, "/");                                      //'//'가 있다면 이 뒤부터, 없다면 uri 처음부터
      if (pos2 != NULL) {                                      // path가 있는 경우
        *pos2 = '\0';                                               // '/' 앞까지 string으로 끊어서
        sscanf(pos, "%s", host);                                    // host에 넣어주기
        *pos2 = '/';                                                // 원상복귀
        sscanf(pos2, "%s", path);                                   // '/'부터 path에 넣어주기
      } else {                                                  // path가 없는 경우 == port 번호도 없고 path 도 없다
        sscanf(pos, "%s", host);
      }
    }
    if (strlen(host) == 0) strcpy(host, end_server_host);           // host명이 없는 경우 지정

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
  sprintf(portStr, "%d", port);  //포트번호를 int -> char*
  return Open_clientfd(hostname, portStr);
}