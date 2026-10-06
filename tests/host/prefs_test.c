#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "dcr_prefs.h"
static const char *root;
const char *dcr_game_root(void){return root;}
void debugPrintf(const char *f,...){(void)f;}
void fatal_error(const char *f,...){va_list a;va_start(a,f);vfprintf(stderr,f,a);va_end(a);abort();}
static int publish_failure;
int host_rename(const char *a,const char *b){
 if(publish_failure && strstr(a,".tmp")){errno=EIO;return -1;}return rename(a,b);
}
static int put(DcrPrefs *p,const char *v){DcrPrefsEdit *e=dcr_prefs_edit(p);dcr_prefs_put(e,'s',"key & < > ",v);int r=dcr_prefs_commit(e);dcr_prefs_edit_free(e);return r;}
static void value(DcrPrefs *p,const char *expected){char t;char *s=dcr_prefs_get(p,"key & < > ",&t);assert(s && t=='s' && !strcmp(s,expected));free(s);}
int main(int argc,char **argv){assert(argc==3);root=argv[1];DcrPrefs *p=dcr_prefs_open("roundtrip");
 if(!strcmp(argv[2],"write")){
  assert(!put(p,"one & < > \" ' UTF-8: İĞö"));value(p,"one & < > \" ' UTF-8: İĞö");
  for(int i=0;i<32;i++){char name[40],v[40];sprintf(name,"store%d",i);sprintf(v,"value%d",i);DcrPrefs *q=dcr_prefs_open(name);assert(!put(q,v));value(q,v);}
  value(p,"one & < > \" ' UTF-8: İĞö");
  publish_failure=1;assert(put(p,"must not replace")!=0);value(p,"one & < > \" ' UTF-8: İĞö");publish_failure=0;
 }else{value(p,"one & < > \" ' UTF-8: İĞö");}
 puts("preferences: isolation, escaping, restart/recovery and failed-publish rollback passed");return 0;
}
