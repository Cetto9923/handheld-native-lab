/* Copy reviewed module TLS data after loader relocation; execute no game code.
 * Original template bytes and generated image are written only to --out's local
 * experiment directory by the Python driver. No game bytes are in this source. */
#include "../../src/runtime/hle/module_tls.h"
#include "../../src/runtime/nso/nso_loader.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_bytes(const char *directory,const char *name,const void *data,size_t size) {
    char path[4096];
    int n=snprintf(path,sizeof(path),"%s/%s",directory,name);
    if(n<0 || (size_t)n>=sizeof(path))return 1;
    FILE *f=fopen(path,"wb");
    if(!f)return 1;
    int failed=fwrite(data,1,size,f)!=size;
    if(fclose(f))failed=1;
    return failed;
}
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    char err[256],build_id[33];
    nso_module *module=nso_load_file(argv[1],NULL,err,sizeof(err));
    if(!module) {fprintf(stderr,"%s\n",err);return 1;}
    nso_build_id(module,build_id);
    if(strcmp(build_id,"736837afd390442ed7c74a7de730e71b") ||
       nso_image_size(module)<0x7767108+0x220) {nso_free(module);return 1;}
    unsigned char snapshot[0x220];
    const void *mapped_source=(const void*)(uintptr_t)nso_addr(module,0x7767108);
    memcpy(snapshot,mapped_source,sizeof(snapshot));
    int errors=write_bytes(argv[2],"loaded-template.bin",mapped_source,sizeof(snapshot));
    nso_module_tls_template source={mapped_source,0x220,0x1019,8,16};
    nso_module_tls_image *first=NULL,*second=NULL;
    if(nso_module_tls_create(&source,&first) || nso_module_tls_create(&source,&second)) {
        nso_module_tls_free(first);nso_module_tls_free(second);nso_free(module);return 1;
    }
    if(memcmp(mapped_source,snapshot,sizeof(snapshot)))errors++;
    /* Once synchronous copies finish, neither image relies on the source module.
     * No initializer or original game instruction was called. */
    nso_free(module);module=NULL;source.initialized_data=NULL;
    const nso_module_tls_view *a=nso_module_tls_get_view(first),*b=nso_module_tls_get_view(second);
    if(a->payload!=a->anchor+16 || b->payload!=b->anchor+16 ||
       (uintptr_t)a->payload%8 || (uintptr_t)b->payload%8 ||
       a->storage_size!=0x1030 || b->storage_size!=0x1030 ||
       a->anchor==b->anchor || memcmp(a->payload,snapshot,sizeof(snapshot)) ||
       memcmp(b->payload,snapshot,sizeof(snapshot)))errors++;
    for(size_t i=0;i<16;i++)if(a->anchor[i] || b->anchor[i])errors++;
    for(size_t i=0x220;i<0x1019;i++)if(a->payload[i] || b->payload[i])errors++;
    for(size_t i=0x1029;i<0x1030;i++)if(a->anchor[i] || b->anchor[i])errors++;
    errors+=write_bytes(argv[2],"copied-image.bin",b->anchor,b->storage_size);
    a->payload[0]^=0xff;a->payload[0x1018]=0x57;
    if(memcmp(b->payload,snapshot,sizeof(snapshot)) || b->anchor[0x1028])errors++;
    nso_module_tls_free(first);
    if(memcmp(b->payload,snapshot,sizeof(snapshot)))errors++;
    nso_module_tls_free(second);
    printf("{\"build_id\":\"%s\",\"module_template_offset\":\"0x7767108\","
           "\"initialized\":544,\"total\":4121,\"zero_tail\":3577,\"alignment\":8,"
           "\"header\":16,\"storage\":4144,\"copies\":2,\"source_module_unloaded\":true,"
           "\"game_code_executed\":false,\"sdk_tls_bound\":false,\"errors\":%d}\n",build_id,errors);
    return errors?1:0;
}
