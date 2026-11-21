#define _POSIX_C_SOURCE 200809L
static char intercom_init[512], passive_init[512];
#define LE_INIT_INTERCOMD intercom_init
#define LE_INIT_AGENTD passive_init
#define LE_INIT_STTD passive_init
#define LE_INIT_TTSD passive_init
#define LE_INIT_ESPHOMED "/absent-intercom-fixture/esphomed.init"
#define LE_INIT_MDNSD "/absent-intercom-fixture/mdnsd.init"
#include "../src/api.c"
#include <assert.h>
int main(int argc,char **argv)
{
    assert(argc==3);
    assert(strlen(argv[1])<sizeof intercom_init&&strlen(argv[2])<sizeof passive_init);
    strcpy(intercom_init,argv[1]);strcpy(passive_init,argv[2]);
    assert(voice_pipeline_restart("local")==LE_OK);
    puts("API mode exit stopped the real optional intercom companion before local voice: PASS");
    return 0;
}
