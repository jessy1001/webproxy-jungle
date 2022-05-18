/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr; //sockaddr_storage structure is large enough to hold any type of socket address -> protocol independent

  /* Check command line args */
  if (argc != 2) { //need 
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);                        //open a listening socket on this port
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,            //waits for a connection request form a client to arrive on listenfd, fills in the client's socket address in clientaddr
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);                                         //converts the socket address structure to the corresponding host and service name strings and copies them to the host and service buffers.
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
}

void doit(int fd) {                                         //fd is connect file descriptor
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /*Read request line and headers*/
  Rio_readinitb(&rio, fd);                                  //sets internal buff so that it will read connfd from start
  Rio_readlineb(&rio, buf, MAXLINE);                        //read request line //read from connfd into user buf until meet '\n'
  printf("Request headers:\n");
  printf("%s", buf);                                        //print request line //print first line of input
  sscanf(buf, "%s %s %s", method, uri, version);            //read from buffer as format <string string string> into method, uri, version
  // if (strcasecmp(method, "HEAD") == 0) method = 1;
  if(strcasecmp(method, "GET") && strcasecmp(method, "HEAD")){                            //if method is something other than "GET" (같지 않으면 길이 차이를 return != 0)
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);                                   //print request headers (TINY does not use info in request headers)

  /*Parse URI from GET request */
  is_static = parse_uri(uri, filename, cgiargs);            //1 if static, 0 if dynamic
  if (stat(filename, &sbuf) < 0){                           //if filename cannot be found in sbuf (copy attributes of filename into sbuf)
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static){/*Serve static content*/
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){     //verify that the file is a regular file and that we have read permission
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read this file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method);  
  }
  else { /*Serve dynamic content*/
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method);
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
  char buf[MAXLINE], body[MAXBUF];

  /*Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /*Print the HTTP response*/
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));                         //fd is connect file descriptor
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));

}

void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);                          //read one line from rp into buffer
  while(strcmp(buf, "\r\n")){                               //if not(buff == "\r\n")
    Rio_readlineb(rp, buf, MAXLINE);                        //read that line into buffer
    printf("%s", buf);                                      //and print it
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs){  //parse uri into a filename and an optional CGI argument string
  char *ptr;
  
  if (!strstr(uri, "cgi-bin")){ /* Static content */      //strstr() finds the first occurrence of the substring "cgi-bin" in uri
    strcpy(cgiargs, "");                                  //leave cgiargs blank
    strcpy(filename, ".");                                //copy "." to filename
    strcat(filename, uri);                                //현재 filename: .uri
    if(uri[strlen(uri)-1] == '/')                         //uri(that ends with /)일 경우
      strcat(filename, "home.html");                      //filename: .uri(/)home.html
    return 1;                                             //return 1 if static content
  }
  else { /*Dynamic content */
    ptr = index(uri, '?');                                //locates the first occurrence of '?' in string pointed to by uri
    if(ptr){                                              //if the uri has arguments
      strcpy(cgiargs, ptr+1);                             //'?' 바로 뒤
      *ptr = '\0';                                        //'?'자리에 '\0'to indicate that string ends there -> filename에 여기까지만 들어감('?' 전까지)
    }
    else strcpy(cgiargs, "");

    strcpy(filename, ".");
    strcat(filename, uri);                                //현재 filename: .uri
    return 0;                                             //return 0 if dynamic content
  }
}

void serve_static(int fd, char *filename, int filesize, char *method){
  int srcfd;
  char filetype[MAXLINE], buf[MAXBUF], *fbuf;
  // char *srcp, 

  /*Send response headers to client*/
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));                           //send buffer(response headers) to fd(client)
  printf("Response headers:\n");
  printf("%s", buf);

  if (strcasecmp(method, "HEAD")){                              //if method is not "HEAD"
    /*Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);                        //opens filename for reading and gets its descriptor
    fbuf = malloc(filesize);                                    //allocate memory in virtual memory
    Rio_readn(srcfd, fbuf, filesize);                           //read from srcfd to fbuf
    Close(srcfd);
    Rio_writen(fd, fbuf, filesize);                             //write from fbuf to fd
    free(fbuf);
  
  }
  /* Mmap, Munmap version*/
  // srcfd = Open(filename, O_RDONLY, 0);                        //opens filename for reading and gets its descriptor
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //map srcfd to 0 in virtual memory, returns pointer to mapped area
  // Close(srcfd);
  // Rio_writen(fd, srcp, filesize);
  // Munmap(srcp, filesize);
}

/*
*get_filetype - Derive file type from filename
*/
void get_filetype(char *filename, char *filetype){
  if (strstr(filename, ".html")) strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif")) strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png")) strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg")) strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4")) strcpy(filetype, "video/mp4");
  else
  strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method){
  char buf[MAXLINE], *emptylist[] = {NULL};

  /*Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  
  if(Fork() == 0) { /* Child */
    /* Real server would call all CGI vars here*/
    setenv("QUERY_STRING", cgiargs, 1);           
    setenv("REQUEST_METHOD", method, 1);            //send method to adder.c
    Dup2(fd, STDOUT_FILENO);   /*Redirect stdout to client */
    Execve(filename, emptylist, environ); /* Run CGI program*/    
  }
  Wait(NULL); /*Parent waits for and reaps child */
  
}