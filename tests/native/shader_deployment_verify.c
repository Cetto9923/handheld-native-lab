/* Hash the actual device resources against a separately generated manifest.
 * The manifest/game bytes stay private in .local; no generated data here. */
#include "../../src/graphics/nvn_device/nvn_sha256.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
int main(int argc,char **argv){if(argc!=3)return 2;FILE *list=fopen(argv[1],"r");if(!list)return 3;char name[64],expected[65],path[1024],actual[65];unsigned char data[65536],digest[32];unsigned checked=0,errors=0;uint64_t total=0;
 while(fscanf(list,"%63s %64s",name,expected)==2){
 if(strlen(name)!=20||strcmp(name+16,".xgs")||strspn(name,"0123456789abcdef")!=16||strlen(expected)!=64)return 4;
 snprintf(path,sizeof path,"%s/%s",argv[2],name);FILE *f=fopen(path,"rb");if(!f){errors++;fprintf(stderr,"MISSING %s\n",name);continue;}
 nvn_sha256_context h;nvn_sha256_init(&h);size_t n;while((n=fread(data,1,sizeof data,f))){total+=n;nvn_sha256_update(&h,data,n);}if(ferror(f))errors++;fclose(f);nvn_sha256_final(&h,digest);
 static const char hex[]="0123456789abcdef";for(unsigned i=0;i<32;i++){actual[2*i]=hex[digest[i]>>4];actual[2*i+1]=hex[digest[i]&15];}actual[64]=0;
 if(strcmp(actual,expected)){errors++;fprintf(stderr,"MISMATCH %s\n",name);}checked++;
 if(checked%5000==0){printf("verified=%u errors=%u bytes=%llu\n",checked,errors,(unsigned long long)total);fflush(stdout);}
 }fclose(list);printf("shader_deployment_verify files=%u errors=%u bytes=%llu\n",checked,errors,(unsigned long long)total);return errors?1:0;}
