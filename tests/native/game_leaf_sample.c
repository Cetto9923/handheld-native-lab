/* Native execution oracle for a byte-identified, unnamed game leaf routine.
 * This harness contains no copied target instructions or generated game code.
 * Only the loader executes the original private NSO's instructions.
 */
#include "../../src/runtime/nso/nso_loader.h"
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Independent mathematical oracle: signed maximum, then unsigned bit length.
 * The result describes this one routine; its engine-facing name is unknown. */
static uint32_t expected(int32_t a,int32_t b,int32_t c,uint32_t flags) {
    int32_t max=a>b?a:b;
    int32_t third=(flags&1)?c:1;
    if(third>max)max=third;
    uint32_t n=(uint32_t)max|1u,width=0;
    do {width++;n/=2;} while(n);
    return width;
}
int main(int argc,char **argv) {
    if(argc!=2){fprintf(stderr,"usage: %s <main.bin>\n",argv[0]);return 2;}
    char err[256],bid[33];
    nso_module *m=nso_load_file(argv[1],NULL,err,sizeof(err));
    if(!m){fprintf(stderr,"%s\n",err);return 1;}
    nso_build_id(m,bid);
    if(strcmp(bid,"736837afd390442ed7c74a7de730e71b")){
        fprintf(stderr,"sample not identified for this build id\n");nso_free(m);return 1;
    }
    nso_install_fault_trap(m);
    typedef uint32_t(*leaf_fn)(int32_t,int32_t,int32_t,uint32_t);
    leaf_fn leaf=(leaf_fn)(uintptr_t)nso_addr(m,0x694bd0);
    int32_t values[]={INT32_MIN,-128,-1,0,1,2,3,7,8,15,16,255,256,65535,INT32_MAX};
    uint32_t flags[]={0,1,2,3,UINT32_MAX};
    uint32_t cases=0,failures=0;
    uint64_t checksum=0;
    for(size_t a=0;a<sizeof(values)/sizeof(*values);a++)
        for(size_t b=0;b<sizeof(values)/sizeof(*values);b++)
            for(size_t c=0;c<sizeof(values)/sizeof(*values);c++)
                for(size_t f=0;f<sizeof(flags)/sizeof(*flags);f++) {
                    uint32_t want=expected(values[a],values[b],values[c],flags[f]);
                    uint32_t got=leaf(values[a],values[b],values[c],flags[f]);
                    checksum=checksum*131+got;cases++;
                    if(got!=want) {
                        fprintf(stderr,"mismatch (%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRIu32 ") actual=%u expected=%u\n",
                                values[a],values[b],values[c],flags[f],got,want);
                        failures++;
                    }
                }
    const int32_t examples[][4]={{0,0,0,0},{3,9,100,0},{3,9,100,1},{255,256,0,0},{-8,-2,-1,1},{7,8,1024,2}};
    for(size_t i=0;i<sizeof(examples)/sizeof(*examples);i++) {
        const int32_t *e=examples[i];
        printf("case (%" PRId32 ",%" PRId32 ",%" PRId32 ",%" PRId32 ") -> %u expected=%u\n",
               e[0],e[1],e[2],e[3],leaf(e[0],e[1],e[2],(uint32_t)e[3]),expected(e[0],e[1],e[2],(uint32_t)e[3]));
    }
    printf("{\"module_offset\":\"0x694bd0\",\"cases\":%u,\"failures\":%u,\"checksum\":\"0x%" PRIx64 "\",\"imports_invoked\":0}\n",
           cases,failures,checksum);
    nso_free(m);return failures?1:0;
}
