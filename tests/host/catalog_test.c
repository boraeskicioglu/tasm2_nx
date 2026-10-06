#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
static const char *input;
void debugPrintf(const char *f,...){(void)f;}
uint8_t *tasm2_catalog_read(size_t *size){FILE *f=fopen(input,"rb");if(!f)return NULL;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);uint8_t *b=calloc(1,n+1);*size=n;fread(b,1,n,f);fclose(f);return b;}
const char *tasm2_catalog_json(void),*tasm2_shop_catalog_json(void);
int main(int argc,char **argv){if(argc<2||argc>3)return 2;input=argv[1];const char *out=argc==3?tasm2_shop_catalog_json():tasm2_catalog_json();puts(out?out:"null");return 0;}
