/* Real mock-server transport regressions; no adapter or hardware operations. */
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define CLIENTS 16
#define DEADLINE_BOUND_MS 20000
static struct sockaddr_in address;
static void fail(const char *message){fprintf(stderr,"http client state: %s\n",message);exit(1);}
static long long now_ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))fail("clock_gettime failed");return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static void pause_ms(long ms){struct timespec t={ms/1000,(ms%1000)*1000000};while(nanosleep(&t,&t)<0)if(errno!=EINTR)fail("nanosleep failed");}
static int connect_client(void){int fd=socket(AF_INET,SOCK_STREAM,0);struct timeval timeout={3,0};if(fd<0)fail("socket failed");if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))||setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))||connect(fd,(struct sockaddr*)&address,sizeof(address)))fail("connect/setup failed");return fd;}
static void send_bytes(int fd,const char *text){size_t left=strlen(text);while(left){ssize_t n=send(fd,text,left,0);if(n<=0)fail("send failed");text+=n;left-=(size_t)n;}}
static void valid_connected_request(int fd){char reply[1024];size_t used=0;ssize_t n;send_bytes(fd,"GET /openapi.json HTTP/1.1\r\nHost: localhost\r\n\r\n");while(used+1<sizeof(reply)){n=recv(fd,reply+used,sizeof(reply)-used-1,0);if(n<0)fail("valid request recv failed");if(!n)break;used+=(size_t)n;reply[used]=0;if(strstr(reply,"\r\n\r\n"))break;}close(fd);reply[used]=0;if(strncmp(reply,"HTTP/1.1 200 OK\r\n",17)||!strstr(reply,"Content-Type: application/json"))fail("reused slot did not serve the valid request (expected 200 JSON)");}
static void valid_request(void){valid_connected_request(connect_client());}
static void reuse_test(void){static const char *partial[]={"GET /missing.js HTTP/1.1\r\nHost: localhost\r\nX-Stale: ","GET /missing.js HTTP/1.1\r\nHost: localhost\r\nContent-Length: 1000\r\n\r\na"};size_t i;for(i=0;i<sizeof(partial)/sizeof(partial[0]);i++){int fd=connect_client();send_bytes(fd,partial[i]);pause_ms(200);close(fd);pause_ms(200);valid_request();}puts("http client state: partial header/body disconnect and slot reuse: ok");}
static void deadline_test(void){int fd[CLIENTS],i;long long started=now_ms(),end;struct pollfd p[CLIENTS];for(i=0;i<CLIENTS;i++){fd[i]=connect_client();if(i%3==1)send_bytes(fd[i],"GET /openapi.json HTTP/1.1\r\nHost: localhost\r\nX-Slow: ");if(i%3==2)send_bytes(fd[i],"GET /openapi.json HTTP/1.1\r\nHost: localhost\r\nContent-Length: 100\r\n\r\na");pause_ms(50);}pause_ms(200);for(i=0;i<CLIENTS;i++){char byte;ssize_t n=recv(fd[i],&byte,1,MSG_DONTWAIT);if(n>=0||(errno!=EAGAIN&&errno!=EWOULDBLOCK))fail("incomplete connection closed before the deadline");}
/* New bytes must not renew the absolute header/body deadline. */
while(now_ms()-started<10000)pause_ms(50);
for(i=0;i<CLIENTS;i++)if(i%3)send_bytes(fd[i],"b");
end=started+DEADLINE_BOUND_MS;
for(i=0;i<CLIENTS;i++){p[i].fd=fd[i];p[i].events=POLLIN;p[i].revents=0;}
for(;;){int left=0;long long remaining=end-now_ms();for(i=0;i<CLIENTS;i++)if(p[i].fd>=0)left++;if(!left)break;if(remaining<=0)fail("idle/incomplete clients were not closed within 20 seconds");if(poll(p,CLIENTS,(int)remaining)<0){if(errno==EINTR)continue;fail("poll failed");}for(i=0;i<CLIENTS;i++)if(p[i].fd>=0&&p[i].revents){char byte;ssize_t n=recv(p[i].fd,&byte,1,0);if(n>0)fail("incomplete request unexpectedly produced a response");if(n<0&&errno!=ECONNRESET)fail("deadline recv failed");close(p[i].fd);p[i].fd=-1;}}
valid_request();puts("http client state: all 16 idle/partial header/body slots expire despite new bytes; subsequent request: ok");}
static void rejected_response(int fd)
{
    char reply[4096];
    size_t used=0;
    ssize_t n;
    char *body,*length;
    while((n=recv(fd,reply+used,sizeof(reply)-used-1,0))>0){
        used+=(size_t)n;
        if(used+1==sizeof(reply))fail("rejected upload response is unbounded");
    }
    if(n<0)fail("rejected upload reset instead of delivering a complete response and EOF");
    reply[used]=0;
    body=strstr(reply,"\r\n\r\n");
    length=strstr(reply,"\r\nContent-Length: ");
    if(strncmp(reply,"HTTP/1.1 403 Forbidden\r\n",24)||!body||!length||
       strtoul(length+18,NULL,10)!=used-(size_t)(body+4-reply))
        fail("rejected upload did not preserve its complete 403 response");
}
static void rejected_body_test(void)
{
    static char request[71000];
    static const char headers[]="PUT /api/v1/sounds/wake HTTP/1.1\r\nHost: localhost\r\nContent-Type: audio/wav\r\nContent-Length: 70000\r\n\r\n";
    int fd=connect_client(),idle[CLIENTS],fresh[CLIENTS],i;
    long long started;
    size_t head=sizeof(headers)-1;
    memcpy(request,headers,head);
    memset(request+head,'x',70000);
    request[head+70000]=0;
    send_bytes(fd,request);
    rejected_response(fd);
    close(fd);
    /* A client can stop its upload as soon as it sees the 403. Retain that
       connection, without sending the rest, while an unrelated request runs. */
    fd=connect_client();send_bytes(fd,headers);rejected_response(fd);
    started=now_ms();valid_request();
    if(now_ms()-started>1000)fail("a rejected partial upload blocked the HTTP loop");
    close(fd);
    /* Filling every drain slot must not turn a short close grace period into
       the ordinary 15-second incomplete-request budget or a renewed deadline. */
    for(i=0;i<CLIENTS;i++){idle[i]=connect_client();send_bytes(idle[i],headers);rejected_response(idle[i]);}
    pause_ms(700);
    for(i=0;i<CLIENTS;i++)send_bytes(idle[i],"x");
    pause_ms(1100);
    for(i=0;i<CLIENTS;i++)fresh[i]=connect_client();
    pause_ms(200);
    for(i=0;i<CLIENTS;i++)valid_connected_request(fresh[i]);
    for(i=0;i<CLIENTS;i++)close(idle[i]);
    puts("http client state: rejected upload preserves 403 without reset, drains asynchronously and expires within two seconds: ok");
}
int main(int argc,char **argv){const char *url=getenv("LIBREECHO_TEST_URL");char host[64];unsigned port;char extra;int reuse=argc==1||(argc==2&&!strcmp(argv[1],"reuse"));int deadline=argc==1||(argc==2&&!strcmp(argv[1],"deadline"));int rejected=argc==1||(argc==2&&!strcmp(argv[1],"rejected"));if(!reuse&&!deadline&&!rejected)fail("usage: test-http-client-state [reuse|deadline|rejected]");if(!url)url="http://127.0.0.1:18082";if(sscanf(url,"http://%63[^:]:%u%c",host,&port,&extra)!=2||port<1||port>65535)fail("expected LIBREECHO_TEST_URL=http://IPv4:port");memset(&address,0,sizeof(address));address.sin_family=AF_INET;address.sin_port=htons((unsigned short)port);if(inet_pton(AF_INET,host,&address.sin_addr)!=1)fail("URL host must be numeric IPv4");signal(SIGPIPE,SIG_IGN);if(reuse)reuse_test();if(deadline)deadline_test();if(rejected)rejected_body_test();return 0;}
