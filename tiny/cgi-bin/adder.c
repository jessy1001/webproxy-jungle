/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
/* $begin adder */
#include "csapp.h"

int main(void) {
  char *buf, *p;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE];
  int n1 = 0, n2 = 0;

  /*Extract the two arguments */
  if ((buf = getenv("QUERY_STRING")) != NULL){
    p = strchr(buf, '&');
    *p = '\0';
    strcpy(arg1, buf);
    strcpy(arg2, p+1);
    // n1 = atoi(arg1);
    // n2 = atoi(arg2);
    n1 = atoi(strchr(arg1, '=')+1);           //now URI looks like this: http://IPaddress:<port>/cgi-bin/adder?<name>=<num>&<name>=<num>
    n2 = atoi(strchr(arg2, '=') + 1);         //'=' 다음부터 '&'전까지
  }

  /*Make the response body */
  // sprintf(content, "QUERY_STRING=%s", buf);
  sprintf(content, "Welcome to add.com: ");
  sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
  sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
  sprintf(content, "%sThanks for visiting!\r\n", content);

  
  /*Generate the HTTP response */
  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");

  if (strcmp(getenv("REQUEST_METHOD"), "HEAD")){
  printf("%s", content);
  }
  fflush(stdout);           //프린트 대기열에 남아있는 것 프린트해주고 비움.
  exit(0);
}
/* $end adder */
