/* Replays a pseudo-random key stream through fable_main and hashes every
 * screen call, so two builds of the editor can be compared. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "tui.h"
#include "file_fabgl.h"
#include "tinyspell.h"
#undef fopen
#undef fgetc
#undef fputc
#undef fread
#undef fwrite
#undef fgets
#undef fputs
#undef fclose
#undef ungetc
#undef fseek
#undef ftell

static uint64_t H = 1469598103934665603ULL;
static void mix(const void *p, size_t n){ const unsigned char *b=p; while(n--){ H^=*b++; H*=1099511628211ULL; } }
static void mixi(int tag,int a,int b){ int v[3]={tag,a,b}; mix(v,sizeof v); }

/* ---- fixed clock ---- */
time_t time(time_t *t){ if(t) *t=1793000000; return 1793000000; }

/* ---- fake memory alloc ---- */
/* (esp_heap_caps stub header provides heap_caps_calloc) */

/* ---- tui ---- */
void tui_init(void){mixi(1,0,0);} void tui_restore(void){mixi(2,0,0);}
void tui_frame_begin(void){mixi(3,0,0);} void tui_frame_end(void){mixi(4,0,0);}
void tui_frame_put(const char*s,int n){mixi(5,n,0); mix(s,n);}
void tui_clear(void){mixi(6,0,0);} void tui_gotoxy(int x,int y){mixi(7,x,y);}
void tui_color(int f,int b){mixi(8,f,b);} void tui_putc(int c){mixi(9,c,0);}
void tui_puts(const char*s){mixi(10,(int)strlen(s),0); mix(s,strlen(s));}
void tui_cursor(int v,int b){mixi(11,v,b);} void tui_boot_colors(int d){}
int tui_wifi_scan(char names[][33],int max){return 0;}
int tui_wifi_save(const char*a,const char*b){return 1;}
int tui_wifi_load(char*a,int al,char*b,int bl){return 0;}
void tui_exe_dir(char*o,int l){o[0]=0;}
unsigned long tui_millis(void){static unsigned long m; return m+=7;}
void tui_delay(int ms){}
int tui_list_dir(const char*p,char n[][32],int m){return 0;}
int tui_key_pending(void){return 0;}
static uint32_t lcg=12345;
static uint32_t rnd(void){ lcg=lcg*1664525u+1013904223u; return lcg>>8; }
int tui_shift_down(void){ int v=(rnd()&3)==0; mixi(12,v,0); return v; }

/* ---- key stream ---- */
static int q[8], qn, qi; static long nkeys, given;
static uint32_t kr=1;
static uint32_t krnd(void){ kr=kr*22695477u+1; return kr>>10; }
static void gen(void){
    static const int ext[]={72,80,75,77,71,79,73,81,83,59,60,61,62,63};
    static const int ctl[]={2,24,16,6,18,14,15,19};
    uint32_t r=krnd()%100; qn=qi=0;
    if(r<58){ static const char *ch="abcdefghijklmnopqrstuvwxyz ABCDEFGHIJ.,;'-0123456789 the wrold teh"; q[qn++]=(unsigned char)ch[krnd()%strlen(ch)]; }
    else if(r<66) q[qn++]=13;
    else if(r<72) q[qn++]=8;
    else if(r<90){ q[qn++]=(krnd()&7)?0:224; q[qn++]=ext[krnd()%(sizeof ext/sizeof ext[0])]; }
    else q[qn++]=ctl[krnd()%(sizeof ctl/sizeof ctl[0])];
}
int tui_getch(void){
    if(qi>=qn){
        if(given>=nkeys){ printf("END hash=%016llx keys=%ld\n",(unsigned long long)H,given); exit(0); }
        gen(); given++;
        if(given%200==0) printf("  @%ld hash=%016llx\n",given,(unsigned long long)H);
    }
    mixi(13,q[qi],0);
    return q[qi++];
}

/* ---- fake SD on a real folder ---- */
static const char *root; 
static void full(const char*p,char*o,size_t n){ snprintf(o,n,"%s%s%s",root,p[0]=='/'?"":"/",p); }
FILE*fab_fopen(const char*p,const char*m){ char f[600]; full(p,f,sizeof f); return fopen(f,m); }
int fab_fgetc(FILE*f){return fgetc(f);} int fab_fputc(int c,FILE*f){return fputc(c,f);}
size_t fab_fread(void*b,size_t s,size_t n,FILE*f){return fread(b,s,n,f);}
size_t fab_fwrite(const void*b,size_t s,size_t n,FILE*f){return fwrite(b,s,n,f);}
char*fab_fgets(char*s,int n,FILE*f){return fgets(s,n,f);} int fab_fputs(const char*s,FILE*f){return fputs(s,f)<0?EOF:0;}
int fab_fclose(FILE*f){return fclose(f);} int fab_ungetc(int c,FILE*f){return ungetc(c,f);}
int fab_fseek(FILE*f,long o,int w){return fseek(f,o,w);} long fab_ftell(FILE*f){return ftell(f);}
int fab_remove(const char*p){char f[600];full(p,f,sizeof f);return remove(f);}
int fab_rename(const char*a,const char*b){char x[600],y[600];full(a,x,sizeof x);full(b,y,sizeof y);return rename(x,y);}
int fab_mkdir(const char*p){char f[600];full(p,f,sizeof f);mkdir(f,0755);return 0;}
int fab_exists(const char*p){char f[600];struct stat st;full(p,f,sizeof f);return stat(f,&st)==0;}
int fab_list_dir(const char*p,char n[][32],int m){return 0;}
uint64_t fab_card_total(void){return 8ULL<<30;} uint64_t fab_card_used(void){return 1ULL<<30;}
int fab_dir_entries(const char*p,FabDirEntry*o,int max,int*more){
    char f[600]; struct dirent **nl; int c,i,n=0; full(p,f,sizeof f);
    if(more)*more=0;
    c=scandir(f,&nl,NULL,alphasort); if(c<0) return 0;
    for(i=0;i<c;i++){ if(n<max && strcmp(nl[i]->d_name,".") && strcmp(nl[i]->d_name,"..")){
        char g[800]; struct stat st; snprintf(g,sizeof g,"%s/%s",f,nl[i]->d_name); stat(g,&st);
        snprintf(o[n].name,sizeof o[n].name,"%s",nl[i]->d_name); o[n].size=S_ISDIR(st.st_mode)?0:(uint32_t)st.st_size; o[n].mtime=0; o[n].is_dir=S_ISDIR(st.st_mode)?1:0; n++; }
        free(nl[i]); }
    free(nl); return n; }

/* ---- spell stubs ---- */
int ts_init(const char*d){return 1;}
int ts_check(const char*w){ return strcmp(w,"WROLD")&&strcmp(w,"TEH")&&strcmp(w,"B")&&strcmp(w,"C"); }
int ts_suggest(const char*w,char o[][TS_MAX_WORD],int m){return 0;} void ts_free(void){}
int ts_fix_interactive(char**t,int*l){return 0;}

int fable_main(int argc,char**argv);
int main(int argc,char**argv){
    if(argc<4){fprintf(stderr,"usage: seed nkeys root\n");return 1;}
    kr=(uint32_t)atoi(argv[1])*2654435761u+7; lcg=kr; nkeys=atol(argv[2]); root=argv[3];
    { char *av[]={"fable",NULL}; int r=fable_main(1,av); printf("RETURNED %d hash=%016llx\n",r,(unsigned long long)H); }
    return 0;
}
