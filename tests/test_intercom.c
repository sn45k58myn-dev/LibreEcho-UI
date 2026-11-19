#define _POSIX_C_SOURCE 200809L
static char test_socket_path[108];
#define LE_IC_SOCKET test_socket_path
#include "../src/intercom.c"
#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* Deliberately permissive generic security: the stricter intercom boundary
 * must stand even when the development API would authorize everything. */
int api_request_authorize(struct api_context *c, const struct api_request *q,
                          struct api_response *r) { (void)c; (void)q; (void)r; return 1; }
int api_home_assistant_selected(struct api_context *c) { return !strcmp(c->voice_pipeline_mode,"home-assistant"); }
static void masked(unsigned char *out, const void *data, size_t n, unsigned op, size_t *size)
{
    size_t i, h = n < 126 ? 2 : 4; out[0] = (unsigned char)(128u | op);
    out[1] = (unsigned char)(128u | (n < 126 ? n : 126));
    if (h == 4) { out[2] = (unsigned char)(n >> 8); out[3] = (unsigned char)n; }
    memset(out + h, 42, 4);
    for (i = 0; i < n; i++) out[h + 4 + i] = ((const unsigned char *)data)[i] ^ 42;
    *size = h + 4 + n;
}
static void frames(void)
{
    unsigned char wire[1100], pcm[644] = {0}, out[1025]; size_t n,z,used; unsigned op;
    masked(wire, pcm, sizeof pcm, 2, &n);
    for (size_t i = 0; i < n; i++) assert(le_ic_ws_decode(wire, i, out, &z, &op, &used) == 0);
    assert(le_ic_ws_decode(wire,n,out,&z,&op,&used)==1&&z==644&&op==2&&used==n);
    wire[0] = 2; assert(le_ic_ws_decode(wire,n,out,&z,&op,&used)==-1);
    wire[0] = 0xc2; assert(le_ic_ws_decode(wire,n,out,&z,&op,&used)==-1);
    wire[0] = 0x82; wire[1] = 126; assert(le_ic_ws_decode(wire,n,out,&z,&op,&used)==-1);
    wire[1] = 255; assert(le_ic_ws_decode(wire,n,out,&z,&op,&used)==-1);
    masked(wire, pcm,126,9,&n);assert(le_ic_ws_decode(wire,n,out,&z,&op,&used)==-1);
    masked(wire,pcm,1,8,&n);assert(le_ic_ws_decode(wire,n,out,&z,&op,&used)==-1);
    { char accept[64]; assert(!le_ic_ws_accept("dGhlIHNhbXBsZSBub25jZQ==",accept,sizeof accept));
      assert(!strcmp(accept,"s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));assert(le_ic_ws_accept("bad",accept,sizeof accept)); }
}
static void lease_test(const char *dir)
{
    char path[512]; pid_t pid; uint64_t now = le_ic_now(); int status;
    snprintf(path,sizeof path,"%s/lease",dir);assert(!le_ic_lease_write(path,now));
    assert(le_ic_lease_active(path,now));assert(!le_ic_lease_active(path,now+2001));
    assert(!le_ic_lease_active(path,now-10000));
    pid=fork();assert(pid>=0);
    if(!pid){for(unsigned i=0;i<256;i++)if(le_ic_lease_write(path,le_ic_now()))_exit(1);_exit(0);}
    /* Repeated refresh never exposes the empty live inode to a reader. */
    for(unsigned i=0;i<256;i++)assert(le_ic_lease_active(path,le_ic_now()));
    assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&!WEXITSTATUS(status));
    assert(!chmod(path,0644));assert(!le_ic_lease_active(path,le_ic_now()));unlink(path);
    assert(!symlink("/dev/null",path));assert(!le_ic_lease_active(path,le_ic_now()));unlink(path);
}
static void tick(struct api_context *c, int ws, int local)
{
    struct pollfd p[2];le_ic_pollfds(p);p[0].revents=(short)ws;p[1].revents=(short)local;le_ic_tick(c,p);
}
int main(int argc,char **argv)
{
    static struct api_context c; static struct api_response r;
    struct api_request q; char optin[512],grant[65],old_id[65],new_id[65],body[160];
    struct sockaddr_un addr; int pair[2], listener, local; unsigned char frame[1100],buffer[4096];size_t n;
    assert(argc==2);frames();lease_test(argv[1]);
    snprintf(c.config_path,sizeof c.config_path,"%s/config",argv[1]);
    assert(!config_write_atomic(c.config_path,"{}",2));
    { char link[512],real[512],from_link[512];snprintf(link,sizeof link,"%s/config-link",argv[1]);
      assert(!symlink(c.config_path,link));assert(le_ic_optin_path(c.config_path,real,sizeof real));
      assert(le_ic_optin_path(link,from_link,sizeof from_link)&&!strcmp(real,from_link));unlink(link); }
    snprintf(optin,sizeof optin,"%s.intercom",c.config_path);
    c.auth.enabled=1;strcpy(c.auth.sessions[0].token,"login");c.auth.sessions[0].expires=time(NULL)+3600;
    strcpy(c.voice_pipeline_mode,"home-assistant");strcpy(c.csrf_token,"csrf");
    memset(&q,0,sizeof q);strcpy(q.path,"/api/v1/intercom");strcpy(q.method,"PUT");q.https=1;
    strcpy(q.host,"speaker.test:8443");strcpy(q.origin,"https://speaker.test:8443");
    strcpy(q.authorization,"Bearer login");strcpy(q.csrf,"csrf");q.body="{\"enabled\":true}";q.body_len=strlen(q.body);
    q.https=0;assert(le_ic_api(&c,&q,&r)&&r.status==403);q.https=1;
    q.origin[0]=0;assert(le_ic_api(&c,&q,&r)&&r.status==403);
    strcpy(q.origin,"https://evil.test");c.allow_insecure_lan=1;assert(le_ic_api(&c,&q,&r)&&r.status==403);
    strcpy(q.origin,"https://speaker.test:8443");q.csrf[0]=0;assert(le_ic_api(&c,&q,&r)&&r.status==403);strcpy(q.csrf,"csrf");
    c.auth.enabled=0;assert(le_ic_api(&c,&q,&r)&&r.status==403);c.auth.enabled=1;
    assert(le_ic_api(&c,&q,&r)&&r.status==200&&enabled(&c));
    strcpy(q.method,"POST");strcpy(q.path,"/api/v1/intercom/call");q.body="{}";q.body_len=2;
    assert(le_ic_api(&c,&q,&r)&&r.status==201);strcpy(old_id,call.id);
    assert(le_ic_api(&c,&q,&r)&&r.status==409);
    strcpy(q.method,"DELETE");snprintf(body,sizeof body,"{\"id\":\"%s\"}",old_id);q.body=body;q.body_len=strlen(body);
    assert(le_ic_api(&c,&q,&r)&&r.status==200);
    strcpy(q.method,"POST");q.body="{}";q.body_len=2;assert(le_ic_api(&c,&q,&r)&&r.status==201);strcpy(new_id,call.id);strcpy(grant,call.ticket);
    strcpy(q.method,"DELETE");q.body=body;q.body_len=strlen(body);assert(le_ic_api(&c,&q,&r)&&r.status==409&&!strcmp(new_id,call.id));
    /* Complete both directions through the real WSS relay state machine. */
    snprintf(test_socket_path,sizeof test_socket_path,"%s/companion.sock",argv[1]);
    listener=socket(AF_UNIX,SOCK_SEQPACKET,0);assert(listener>=0);memset(&addr,0,sizeof addr);addr.sun_family=AF_UNIX;strcpy(addr.sun_path,test_socket_path);
    assert(!bind(listener,(struct sockaddr*)&addr,sizeof addr)&&!listen(listener,1));
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));strcpy(q.method,"GET");
    assert(!le_ic_upgrade(&c,&q,pair[0],"dGhlIHNhbXBsZSBub25jZQ==","13","websocket","keep-alive, Upgrade"));
    assert(recv(pair[1],buffer,sizeof buffer,0)>0);
    snprintf(body,sizeof body,"{\"ticket\":\"%s\"}",grant);masked(frame,body,strlen(body),1,&n);assert(send(pair[1],frame,n,0)==(ssize_t)n);tick(&c,POLLIN,0);
    assert(call.authenticated&&!call.ticket[0]);local=accept(listener,NULL,NULL);assert(local>=0);
    assert(recv(local,buffer,sizeof buffer,0)==5&&!memcmp(buffer,"LEIC1",5));assert(send(local,"READY",5,0)==5);tick(&c,0,POLLIN);
    assert(call.ready&&recv(pair[1],buffer,sizeof buffer,0)==7);
    memset(buffer,0,644);masked(frame,buffer,644,2,&n);assert(send(pair[1],frame,n,0)==(ssize_t)n);tick(&c,POLLIN,0);
    assert(recv(local,buffer,sizeof buffer,0)==640);
    memset(buffer,37,640);assert(send(local,buffer,640,0)==640);tick(&c,0,POLLIN);
    assert(recv(pair[1],buffer,sizeof buffer,0)==648&&buffer[0]==0x82&&buffer[8]==37);
    le_auth_logout(&c.auth,"login");tick(&c,0,0);assert(call.ws<0&&call.local<0);
    close(pair[1]);close(local);close(listener);unlink(test_socket_path);unlink(optin);
    puts("intercom framing, lease publication, strict auth, scoped cancellation and duplex bridge: PASS");return 0;
}
