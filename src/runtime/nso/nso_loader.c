#define _GNU_SOURCE
#include "nso_loader.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif
#include <unistd.h>

#if !defined(__aarch64__)
#error "nso_loader executes AArch64 code natively"
#endif
#define NSO_MAGIC 0x304f534eu
#define MOD0_MAGIC 0x30444f4du
#define R_ABS64 0x101u
#define R_ABS32 0x102u
#define R_GLOB_DAT 0x401u
#define R_JUMP_SLOT 0x402u
#define R_RELATIVE 0x403u

struct segment { uint32_t file, mem, size, aux; };
struct import_write {uint64_t offset,expected;uint32_t symbol,type;int64_t addend;};
struct nso_module {
    uint8_t *image;
    size_t image_size;
    const uint8_t *source;
    size_t source_size;
    struct nso_owner_ops owner_ops;
    void *owner,*ticket;
    int committed,ready,closing;
    char *dynstr;
    uint32_t dynstr_size, sym_count, import_count, unresolved_count;
    uint64_t *sym_value, *imports;
    uint16_t *sym_shndx;
    uint32_t *sym_name;
    uint8_t *sym_info;
    uint8_t *import_resolved;
    struct import_write *import_writes;
    size_t import_write_count,bound_import_count;
    uint64_t writable_lo,writable_hi;
    int executor_required;
    uint8_t *import_traps, *object_traps;
    size_t trap_size;
    uint64_t rela_offset, rela_size, jmprel_offset, jmprel_size;
    uint64_t relr_offset, relr_size, reloc_count, relr_count;
    uint64_t init_offset, init_array_offset, init_array_size;
    uint64_t text_lo, text_hi;
    uint8_t build_id[16];
    void (*unload_observer)(void *);
    void *unload_user;
};
/* Diagnostic state belongs to the thread that explicitly installed it. The
 * signal disposition itself is process-wide; this remains an opt-in fatal
 * experiment facility, not a recoverable multi-thread exception mechanism. */
static _Thread_local nso_module *fault_module;
static _Thread_local uintptr_t stack_lo,stack_hi;
static _Thread_local unsigned char signal_stack[64*1024];
static _Thread_local stack_t previous_signal_stack;
static _Thread_local int owns_signal_stack;
static _Thread_local struct sigaction previous_actions[4];
static _Thread_local unsigned owns_actions;
static const int fault_signals[4]={SIGSEGV,SIGBUS,SIGILL,SIGTRAP};
static uint16_t rd16(const void *p) { uint16_t v; memcpy(&v,p,2); return v; }
static uint32_t rd32(const void *p) { uint32_t v; memcpy(&v,p,4); return v; }
static uint64_t rd64(const void *p) { uint64_t v; memcpy(&v,p,8); return v; }
static void wr64(void *p, uint64_t v) { memcpy(p,&v,8); }
static size_t pagesize(void) { return (size_t)sysconf(_SC_PAGESIZE); }
static uint64_t down(uint64_t v) { return v & ~(uint64_t)(pagesize()-1); }
static uint64_t up(uint64_t v) { return down(v+pagesize()-1); }
static int range(uint64_t off, uint64_t n, uint64_t limit) { return off <= limit && n <= limit-off; }
static int file_range(const struct segment s[3], uint64_t addr, uint64_t len, uint64_t *off) {
    if (!len) { *off=0; return 1; }
    for(int i=0;i<3;i++) if(addr>=s[i].mem && range(addr-s[i].mem,len,s[i].size)) {
        *off=(uint64_t)s[i].file+addr-s[i].mem; return 1;
    }
    return 0;
}
static void error(char *buf, size_t len, const char *msg) { if(buf && len) snprintf(buf,len,"%s",msg); }

#if defined(NSO_LOADER_TESTING)
static _Thread_local void *(*test_map)(size_t,int,int);
static _Thread_local int (*test_protect)(void *,size_t,int);
static _Thread_local int (*test_unmap)(void *,size_t);
void nso_loader_test_vm(void *(*map)(size_t,int,int),int (*protect)(void *,size_t,int),
        int (*unmap)(void *,size_t)) {test_map=map;test_protect=protect;test_unmap=unmap;}
#endif
static void *loader_map(size_t bytes,int protection,int fd) {
#if defined(NSO_LOADER_TESTING)
    if(test_map)return test_map(bytes,protection,fd);
#endif
    return mmap(NULL,bytes,protection,MAP_PRIVATE|(fd<0?MAP_ANONYMOUS:0),fd,0);
}
static int loader_protect(void *address,size_t bytes,int protection) {
#if defined(NSO_LOADER_TESTING)
    if(test_protect)return test_protect(address,bytes,protection);
#endif
    return mprotect(address,bytes,protection);
}
static int loader_unmap(void *address,size_t bytes) {
#if defined(NSO_LOADER_TESTING)
    if(test_unmap)return test_unmap(address,bytes);
#endif
    return munmap(address,bytes);
}
static struct nso_result load_status(enum nso_status status,int native,int cleanup) {
    return (struct nso_result){status,native,cleanup};
}

static struct nso_load_result load_file(const char *path,nso_import_fn resolve,
        const struct nso_owner_ops *ops,void *owner,char *err,size_t errlen) {
    int fd=-1;
    const uint8_t *file=MAP_FAILED;
    size_t fsize=0;
    nso_module *m=NULL;
    struct nso_result status=load_status(NSO_LOAD_ERROR,0,0);
    const char *failure="unknown load failure";
#define FAIL(msg) do { failure=(msg); goto fail; } while(0)
#define CHECK(cond,msg) do { if(!(cond)) FAIL(msg); } while(0)
#define CHECK_SYS(cond,msg) do { if(!(cond)) {status.native_error=errno;FAIL(msg);} } while(0)
    CHECK(path,"missing path");
    m=calloc(1,sizeof(*m)); CHECK(m,"module allocation failed");
    if(ops) {m->owner_ops=*ops;m->owner=owner;}
    fd=open(path,O_RDONLY);
    CHECK_SYS(fd>=0,"cannot open file");
    struct stat st;
    CHECK_SYS(fstat(fd,&st)==0,"file stat failed");
    CHECK(st.st_size>=0x100,"file smaller than NSO header");
    fsize=(size_t)st.st_size;
    file=loader_map(fsize,PROT_READ,fd);
    int map_error=errno;
    close(fd); fd=-1;
    if(file==MAP_FAILED) {status.native_error=map_error;FAIL("file mmap failed");}
    m->source=file;m->source_size=fsize;
    CHECK(rd32(file)==NSO_MAGIC,"not an NSO0 file");
    CHECK(!(rd32(file+12)&7),"compressed NSO segments unsupported; supply uncompressed image");
    struct segment s[3];
    uint64_t end=0;
    for(int i=0;i<3;i++) {
        s[i]=(struct segment){rd32(file+16+i*16),rd32(file+20+i*16),rd32(file+24+i*16),rd32(file+28+i*16)};
        CHECK(range(s[i].file,s[i].size,fsize),"segment out of file range");
        uint64_t hi=(uint64_t)s[i].mem+s[i].size;
        if(i==2) hi+=s[i].aux;
        if(hi>end) end=hi;
        if(i) CHECK((uint64_t)s[i-1].mem+s[i-1].size<=s[i].mem,"overlapping NSO segments");
    }
    CHECK(end && end<=SIZE_MAX-pagesize(),"invalid image extent");
    m->image_size=(size_t)up(end);
    m->text_lo=s[0].mem; m->text_hi=(uint64_t)s[0].mem+s[0].size;
    memcpy(m->build_id,file+0x40,16);
    if(ops) {
        status=ops->reserve(owner,m->image_size,&m->ticket);
        if(status.status!=NSO_OK)FAIL("module precharge rejected");
        CHECK(m->ticket,"owner returned no charge ticket");
        status=load_status(NSO_LOAD_ERROR,0,0);
    }
    m->image=loader_map(m->image_size,PROT_READ|PROT_WRITE,-1);
    if(m->image==MAP_FAILED) {status.native_error=errno;m->image=NULL;FAIL("module mmap failed");}
    if(ops) {
        status=ops->commit(owner,m->ticket);
        if(status.status!=NSO_OK)FAIL("module charge commit failed");
        m->committed=1;status=load_status(NSO_LOAD_ERROR,0,0);
    }
    for(int i=0;i<3;i++) memcpy(m->image+s[i].mem,file+s[i].file,s[i].size);
    CHECK(s[0].size>=8,"text too short for MOD0 reference");
    /* MOD0 and dynamic pointers are virtual offsets, not file offsets. */
    int64_t mod=(int64_t)s[0].mem+(int32_t)rd32(m->image+s[0].mem+4);
    uint64_t mapped;
    CHECK(mod>=0 && file_range(s,(uint64_t)mod,28,&mapped),"MOD0 out of mapped segments");
    CHECK(rd32(m->image+mod)==MOD0_MAGIC,"MOD0 magic mismatch");
    int64_t dyn=mod+(int32_t)rd32(m->image+mod+4);
    CHECK(dyn>=0,"negative .dynamic offset");
    uint64_t strtab=0,strsz=0,symtab=0,hash=0;
    uint64_t relaent=24,syment=24,relrent=8,pltrel=7;
    int terminated=0;
    for(uint64_t cur=(uint64_t)dyn; file_range(s,cur,16,&mapped);cur+=16) {
        uint64_t tag=rd64(m->image+cur),val=rd64(m->image+cur+8);
        if(!tag) { terminated=1; break; }
        switch(tag) {
        case 2:m->jmprel_size=val;break;
        case 4:hash=val;break;
        case 5:strtab=val;break;
        case 6:symtab=val;break;
        case 7:m->rela_offset=val;break;
        case 8:m->rela_size=val;break;
        case 9:relaent=val;break;
        case 10:strsz=val;break;
        case 11:syment=val;break;
        case 12:m->init_offset=val;break;
        case 20:pltrel=val;break;
        case 23:m->jmprel_offset=val;break;
        case 25:m->init_array_offset=val;break;
        case 27:m->init_array_size=val;break;
        case 35:m->relr_size=val;break;
        case 36:m->relr_offset=val;break;
        case 37:relrent=val;break;
        default:break;
        }
    }
    CHECK(terminated,"unterminated or out-of-segment .dynamic");
    CHECK(syment==24 && relaent==24 && relrent==8 && pltrel==7,"unsupported dynamic entry encoding");
    CHECK(strsz && strsz<=UINT32_MAX && file_range(s,strtab,strsz,&mapped),"invalid dynamic string table");
    if(hash) {
        CHECK(file_range(s,hash,8,&mapped),"invalid DT_HASH header");
        m->sym_count=rd32(m->image+hash+4);
        CHECK(file_range(s,hash,8+4ull*((uint64_t)rd32(m->image+hash)+m->sym_count),&mapped),"invalid DT_HASH extent");
    } else {
        /* GNU-hash-only NSOs supply an exact dynsym extent in their NSO header.
         * Never infer the symbol count from adjacent data or a fixed estimate. */
        uint64_t header_sym=(uint64_t)s[1].mem+rd32(file+0x98);
        uint32_t header_size=rd32(file+0x9c);
        CHECK(header_sym==symtab && header_size%24==0,"missing bounded dynsym extent");
        m->sym_count=header_size/24;
    }
    CHECK(m->sym_count && file_range(s,symtab,(uint64_t)m->sym_count*24,&mapped),"invalid dynamic symbol table");
    m->dynstr_size=(uint32_t)strsz;
    m->dynstr=malloc((size_t)strsz);
    m->sym_name=calloc(m->sym_count,sizeof(*m->sym_name));
    m->sym_shndx=calloc(m->sym_count,sizeof(*m->sym_shndx));
    m->sym_value=calloc(m->sym_count,sizeof(*m->sym_value));
    m->sym_info=calloc(m->sym_count,1);
    m->imports=calloc(m->sym_count,sizeof(*m->imports));
    m->import_resolved=calloc(m->sym_count,1);
    CHECK(m->dynstr && m->sym_name && m->sym_shndx && m->sym_value && m->imports && m->sym_info && m->import_resolved,"symbol allocation failed");
    memcpy(m->dynstr,m->image+strtab,(size_t)strsz);
    /* Per-symbol BRK sentinels supply identity, never target translation.
     * Imported objects use inaccessible slots so reads also fail observably. */
    m->trap_size=(size_t)up((uint64_t)m->sym_count*8);
    m->import_traps=loader_map(m->trap_size,PROT_READ|PROT_WRITE,-1);
    if(m->import_traps==MAP_FAILED) {status.native_error=errno;m->import_traps=NULL;FAIL("import sentinel mmap failed");}
    m->object_traps=loader_map(m->trap_size,PROT_NONE,-1);
    if(m->object_traps==MAP_FAILED) {status.native_error=errno;m->object_traps=NULL;FAIL("object sentinel mmap failed");}
    for(uint32_t i=0;i<m->sym_count;i++) {
        const uint8_t *e=m->image+symtab+(uint64_t)i*24;
        m->sym_name[i]=rd32(e); m->sym_shndx[i]=rd16(e+6); m->sym_value[i]=rd64(e+8); m->sym_info[i]=e[4];
        CHECK(m->sym_name[i]<strsz && memchr(m->dynstr+m->sym_name[i],0,(size_t)strsz-m->sym_name[i]),"invalid symbol name");
        uint32_t brk=0xd4200000u;
        memcpy(m->import_traps+i*8,&brk,4);
        if(i && !m->sym_shndx[i]) {
            m->import_count++;
            m->imports[i]=resolve?resolve(i,m->dynstr+m->sym_name[i]):0;
            if(m->imports[i]) {m->import_resolved[i]=1;m->bound_import_count++;}
            if(!m->imports[i]) {
                m->unresolved_count++;
                m->imports[i]=(uint64_t)(uintptr_t)(((e[4]&15)==1?m->object_traps:m->import_traps)+i*8);
            }
        }
    }
    __builtin___clear_cache((char*)m->import_traps,(char*)m->import_traps+m->trap_size);
    CHECK_SYS(loader_protect(m->import_traps,m->trap_size,PROT_READ|PROT_EXEC)==0,"import sentinel mprotect failed");
    uint64_t base=nso_base(m);
    CHECK(m->relr_size%8==0 && file_range(s,m->relr_offset,m->relr_size,&mapped),"invalid DT_RELR extent");
    uint64_t where=0;
    int have_where=0;
    for(uint64_t k=0;k<m->relr_size;k+=8) {
        uint64_t entry=rd64(m->image+m->relr_offset+k);
        if(!(entry&1)) {
            CHECK(!(entry&7) && range(entry,8,m->image_size),"RELR target out of image");
            where=entry; have_where=1;
            wr64(m->image+where,rd64(m->image+where)+base); m->relr_count++; where+=8;
        } else {
            CHECK(have_where && range(where,63*8,m->image_size),"RELR bitmap out of image");
            for(int b=1;b<64;b++) if((entry>>b)&1) {
                uint64_t target=where+(b-1)*8;
                wr64(m->image+target,rd64(m->image+target)+base); m->relr_count++;
            }
            where+=63*8;
        }
    }
    CHECK(m->rela_size%24==0 && m->jmprel_size%24==0 &&
        file_range(s,m->rela_offset,m->rela_size,&mapped) && file_range(s,m->jmprel_offset,m->jmprel_size,&mapped),"invalid RELA extent");
    CHECK(m->rela_size/24<=SIZE_MAX/sizeof(*m->import_writes) &&
        m->jmprel_size/24<=SIZE_MAX/sizeof(*m->import_writes)-m->rela_size/24,"import write allocation overflow");
    size_t write_capacity=(size_t)(m->rela_size/24+m->jmprel_size/24);
    if(write_capacity) {m->import_writes=calloc(write_capacity,sizeof(*m->import_writes));CHECK(m->import_writes,"import write allocation failed");}
    for(int pass=0;pass<2;pass++) {
        uint64_t off=pass?m->jmprel_offset:m->rela_offset,sz=pass?m->jmprel_size:m->rela_size;
        CHECK(sz%24==0 && file_range(s,off,sz,&mapped),"invalid RELA extent");
        for(uint64_t k=0;k<sz;k+=24) {
            const uint8_t *e=m->image+off+k;
            uint64_t target=rd64(e),info=rd64(e+8),addend=rd64(e+16),value;
            uint32_t type=(uint32_t)info,sym=(uint32_t)(info>>32);
            CHECK(type==R_ABS64 || type==R_ABS32 || type==R_GLOB_DAT || type==R_JUMP_SLOT || type==R_RELATIVE,"unsupported AArch64 relocation");
            CHECK(range(target,type==R_ABS32?4:8,m->image_size),"RELA target out of image");
            if(type==R_RELATIVE) { CHECK(sym==0,"RELATIVE relocation has nonzero symbol"); value=base+addend; }
            else {
                CHECK(sym<m->sym_count,"RELA symbol out of bounds");
                value=m->sym_shndx[sym]?(m->sym_shndx[sym]==0xfff1?m->sym_value[sym]:base+m->sym_value[sym]):m->imports[sym];
                value+=addend;
            }
            if(type==R_ABS32) { CHECK(value<=UINT32_MAX,"ABS32 relocation overflow"); uint32_t v=(uint32_t)value;memcpy(m->image+target,&v,4); }
            else wr64(m->image+target,value);
            if(sym && !m->sym_shndx[sym])
                m->import_writes[m->import_write_count++]=(struct import_write){target,value,sym,type,(int64_t)addend};
            m->reloc_count++;
        }
    }
    CHECK(!m->init_offset || (m->init_offset>=m->text_lo && m->init_offset<m->text_hi),"DT_INIT out of text");
    CHECK(m->init_array_size%8==0 && range(m->init_array_offset,m->init_array_size,m->image_size),"invalid DT_INIT_ARRAY");
    /* Retain shared host-page policy: executable text owns its last page. */
    for(int i=1;i<3;i++) {
        uint64_t hi=(uint64_t)s[i].mem+s[i].size+(i==2?s[i].aux:0);
        if(hi>s[i].mem) CHECK_SYS(loader_protect(m->image+down(s[i].mem),(size_t)(up(hi)-down(s[i].mem)),i==1?PROT_READ:PROT_READ|PROT_WRITE)==0,"data mprotect failed");
    }
    __builtin___clear_cache((char*)m->image+m->text_lo,(char*)m->image+m->text_hi);
    CHECK_SYS(loader_protect(m->image+down(m->text_lo),(size_t)(up(m->text_hi)-down(m->text_lo)),PROT_READ|PROT_EXEC)==0,"text mprotect failed");
    m->writable_lo=down(s[2].mem);
    m->writable_hi=up((uint64_t)s[2].mem+s[2].size+s[2].aux);
    CHECK_SYS(loader_unmap((void*)file,fsize)==0,"source munmap failed");
    m->source=NULL;m->source_size=0;m->ready=1;
    return (struct nso_load_result){load_status(NSO_OK,0,0),m};
fail:
    if(fd>=0) close(fd);
    {struct nso_result cleanup=nso_release(&m);
    if(cleanup.status!=NSO_OK) {status.cleanup_error=cleanup.cleanup_error?cleanup.cleanup_error:cleanup.native_error;status.status=NSO_RETAINED;}
    error(err,errlen,failure);return (struct nso_load_result){status,m};}
#undef CHECK
#undef CHECK_SYS
#undef FAIL
}
struct nso_load_result nso_load_file_owned(const char *path,nso_import_fn resolve,
        const struct nso_owner_ops *ops,void *owner,char *err,size_t errlen) {
    if(!ops || !ops->reserve || !ops->commit || !ops->cancel || !ops->release) {
        error(err,errlen,"complete owner callbacks required");
        return (struct nso_load_result){load_status(NSO_INVALID,0,0),NULL};
    }
    return load_file(path,resolve,ops,owner,err,errlen);
}
nso_module *nso_load_file(const char *path,nso_import_fn resolve,char *err,size_t errlen) {
    struct nso_load_result loaded=load_file(path,resolve,NULL,NULL,err,errlen);
    if(loaded.module && loaded.result.status!=NSO_OK) {
        fprintf(stderr,"[loader-retained] legacy load cannot discard cleanup handle=%p errno=%d\n",
                (void *)loaded.module,loaded.result.cleanup_error);abort();
    }
    return loaded.module;
}
struct nso_result nso_release(nso_module **module) {
    if(!module)return load_status(NSO_INVALID,0,0);
    nso_module *m=*module;
    if(!m)return load_status(NSO_OK,0,0);
    if(fault_module==m) {
        int e=nso_fault_scope_end(m);
        if(e)return load_status(NSO_RETAINED,0,e);
    }
    m->ready=0;m->closing=1;
    if(m->unload_observer) {
        void (*observer)(void*)=m->unload_observer;
        m->unload_observer=NULL;
        observer(m->unload_user);
    }
    int cleanup=0;
#define RELEASE_MAPPING(ptr,bytes) do {if(ptr) {if(loader_unmap((void *)(ptr),(bytes))) {if(!cleanup)cleanup=errno;} else (ptr)=NULL;}} while(0)
    RELEASE_MAPPING(m->source,m->source_size);
    RELEASE_MAPPING(m->image,m->image_size);
    RELEASE_MAPPING(m->import_traps,m->trap_size);
    RELEASE_MAPPING(m->object_traps,m->trap_size);
#undef RELEASE_MAPPING
    if(!m->image && m->ticket) {
        struct nso_result dropped=m->committed?m->owner_ops.release(m->owner,m->ticket):m->owner_ops.cancel(m->owner,m->ticket);
        if(dropped.status==NSO_OK)m->ticket=NULL;
        else if(!cleanup)cleanup=dropped.native_error?dropped.native_error:EIO;
    }
    if(cleanup)return load_status(NSO_RETAINED,0,cleanup);
    free(m->dynstr);free(m->sym_name);free(m->sym_shndx);free(m->sym_value);free(m->imports);free(m->sym_info);free(m->import_resolved);free(m->import_writes);free(m);
    *module=NULL;return load_status(NSO_OK,0,0);
}
void nso_free(nso_module *m) {
    struct nso_result status=nso_release(&m);
    if(status.status!=NSO_OK) {
        fprintf(stderr,"[loader-retained] legacy free cannot discard handle=%p errno=%d\n",(void *)m,status.cleanup_error);
        abort();
    }
}
void nso_set_unload_observer(nso_module *m,void (*observer)(void*),void *user) {
    if(m){m->unload_observer=observer;m->unload_user=user;}
}
uint64_t nso_base(const nso_module *m) {return (uint64_t)(uintptr_t)m->image;}
size_t nso_image_size(const nso_module *m) {return m->image_size;}
size_t nso_bound_import_count(const nso_module *m) {return m?m->bound_import_count:0;}
struct nso_result nso_bound_import_at(const nso_module *m,size_t ordinal,struct nso_import_target *out) {
    if(!m || !out || !m->ready || m->closing)return load_status(NSO_INVALID,EINVAL,0);
    for(uint32_t i=1;i<m->sym_count;i++)if(m->import_resolved[i]) {
        if(!ordinal) {*out=(struct nso_import_target){i,nso_symbol_name(m,i),m->imports[i]};return load_status(NSO_OK,0,0);}
        ordinal--;
    }
    return load_status(NSO_INVALID,ERANGE,0);
}
struct nso_result nso_retarget_imports(nso_module *m,const struct nso_import_target *targets,size_t count) {
    if(!m || !m->ready || m->closing || (count && !targets))return load_status(NSO_INVALID,EINVAL,0);
    for(size_t i=0;i<count;i++) {
        uint32_t symbol=targets[i].index;
        if(!symbol || symbol>=m->sym_count || !m->import_resolved[symbol] || !targets[i].target ||
           (m->sym_info[symbol]&15)!=2)return load_status(NSO_UNSUPPORTED,ENOTSUP,0);
        for(size_t j=0;j<i;j++)if(targets[j].index==symbol)return load_status(NSO_INVALID,EINVAL,0);
        for(size_t j=0;j<m->import_write_count;j++) {
            struct import_write *w=&m->import_writes[j];
            if(w->symbol!=symbol)continue;
            if(w->type==R_ABS32 || w->addend || (w->offset&7) ||
               w->offset<m->writable_lo || !range(w->offset,8,m->writable_hi) ||
               (w->offset<up(m->text_hi) && w->offset+8>down(m->text_lo)))
                return load_status(NSO_UNSUPPORTED,ENOTSUP,0);
            if(rd64(m->image+w->offset)!=w->expected)return load_status(NSO_RETAINED,EBUSY,0);
            for(size_t k=0;k<m->import_write_count;k++)if(k!=j) {
                uint64_t other=m->import_writes[k].offset;
                if(other<w->offset+8 && w->offset<other+(m->import_writes[k].type==R_ABS32?4:8))
                    return load_status(NSO_UNSUPPORTED,ENOTSUP,0);
            }
        }
    }
    /* No fallible operation follows preflight. Owner excludes every guest/user
     * of these mappings; originally writable data pages remain writable. */
    for(size_t i=0;i<count;i++) {
        for(size_t j=0;j<m->import_write_count;j++)if(m->import_writes[j].symbol==targets[i].index) {
            struct import_write *w=&m->import_writes[j];wr64(m->image+w->offset,targets[i].target);w->expected=targets[i].target;
        }
        m->imports[targets[i].index]=targets[i].target;
    }
    return load_status(NSO_OK,0,0);
}
void nso_require_executor(nso_module *m) {if(m)m->executor_required=1;}
struct nso_result nso_entry_address(const nso_module *m,uint64_t off,uint64_t *out) {
    if(!m || !out || !m->ready || m->closing || off<m->text_lo || off>=m->text_hi || off%4)
        return load_status(NSO_INVALID,EINVAL,0);
    *out=(uint64_t)(uintptr_t)(m->image+off);return load_status(NSO_OK,0,0);
}
uint64_t nso_addr(const nso_module *m,uint64_t off) {return off<m->image_size?nso_base(m)+off:0;}
uint32_t nso_import_count(const nso_module *m) {return m->import_count;}
uint32_t nso_unresolved_count(const nso_module *m) {return m->unresolved_count;}
uint64_t nso_reloc_count(const nso_module *m) {return m->reloc_count;}
uint64_t nso_relr_count(const nso_module *m) {return m->relr_count;}
const char *nso_symbol_name(const nso_module *m,uint32_t i) {return i<m->sym_count?m->dynstr+m->sym_name[i]:NULL;}
uint64_t nso_symbol_value(const nso_module *m,uint32_t i) {return i<m->sym_count && m->sym_shndx[i]?m->sym_value[i]:UINT64_MAX;}
uint32_t nso_find_symbol(const nso_module *m,const char *name) {
    if(name)for(uint32_t i=1;i<m->sym_count;i++)if(!strcmp(nso_symbol_name(m,i),name))return i;
    return 0;
}
void nso_build_id(const nso_module *m,char out[33]) {for(int i=0;i<16;i++)snprintf(out+i*2,3,"%02x",m->build_id[i]);}
uint64_t nso_call(nso_module *m,uint64_t off,uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5) {
    uint64_t address;
    if(!m || m->executor_required || nso_entry_address(m,off,&address).status!=NSO_OK) {fprintf(stderr,"[call-error] offset 0x%" PRIx64 " requires a valid executor/ready aligned text\n",off);exit(2);}
    typedef uint64_t(*fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
    return ((fn)(uintptr_t)address)(a0,a1,a2,a3,a4,a5);
}
struct nso_result nso_run_init_execute(nso_module *m,nso_init_policy policy,nso_executor_fn execute,void *user) {
    if(!m || !m->ready || m->closing || !execute)return load_status(NSO_INVALID,EINVAL,0);
    if(policy!=NSO_INIT_ELF && policy!=NSO_INIT_DT_INIT_OWNS_ARRAY) {
        return load_status(NSO_INVALID,EINVAL,0);
    }
    const uint64_t arguments[6]={0};uint64_t value;
    printf("[init] policy=%s\n",policy==NSO_INIT_ELF?"elf":"dt-init-owns-array");fflush(stdout);
    if(m->init_offset) {printf("[init] DT_INIT 0x%" PRIx64 "\n",m->init_offset);fflush(stdout);struct nso_result r=execute(user,m,m->init_offset,arguments,&value);if(r.status!=NSO_OK)return r;}
    if(policy==NSO_INIT_DT_INIT_OWNS_ARRAY) {
        if(m->init_offset) {puts("[init] array belongs to DT_INIT wrapper; no second walk");return load_status(NSO_OK,0,0);}
        puts("[init] no DT_INIT; walking array once");
    }
    for(uint64_t k=0;k<m->init_array_size;k+=8) {
        uint64_t addr=rd64(m->image+m->init_array_offset+k);
        if(!addr || addr==UINT64_MAX)continue;
        printf("[init] array[%" PRIu64 "] offset 0x%" PRIx64 "\n",k/8,addr-nso_base(m));fflush(stdout);
        uint64_t ignored;
        if(addr<nso_base(m) || nso_entry_address(m,addr-nso_base(m),&ignored).status!=NSO_OK)return load_status(NSO_INVALID,EINVAL,0);
        struct nso_result r=execute(user,m,addr-nso_base(m),arguments,&value);if(r.status!=NSO_OK)return r;
    }
    return load_status(NSO_OK,0,0);
}
static struct nso_result raw_executor(void *user,nso_module *m,uint64_t off,const uint64_t a[6],uint64_t *value) {
    (void)user;*value=nso_call(m,off,a[0],a[1],a[2],a[3],a[4],a[5]);return load_status(NSO_OK,0,0);
}
void nso_run_init_policy(nso_module *m,nso_init_policy policy) {
    if(nso_run_init_execute(m,policy,raw_executor,NULL).status!=NSO_OK) {fputs("[init-error] executor/policy failed\n",stderr);exit(2);}
}
void nso_run_init(nso_module *m) {nso_run_init_policy(m,NSO_INIT_ELF);}
static int sentinel_index(const uint8_t *base,size_t size,uint64_t addr) {
    return addr>=(uintptr_t)base && addr-(uintptr_t)base<size?(int)((addr-(uintptr_t)base)/8):-1;
}
static void fault_handler(int sig,siginfo_t *si,void *context) {
    ucontext_t *u=context;
    uint64_t pc=0,lr=0,fp=0,sp=0,fa=si?(uintptr_t)si->si_addr:0,regs[29]={0};
#if defined(__APPLE__)
    pc=u->uc_mcontext->__ss.__pc;lr=u->uc_mcontext->__ss.__lr;fp=u->uc_mcontext->__ss.__fp;sp=u->uc_mcontext->__ss.__sp;
    for(int i=0;i<29;i++)regs[i]=u->uc_mcontext->__ss.__x[i];
#elif defined(__linux__) && defined(__aarch64__)
    pc=u->uc_mcontext.pc;lr=u->uc_mcontext.regs[30];fp=u->uc_mcontext.regs[29];sp=u->uc_mcontext.sp;
    for(int i=0;i<29;i++)regs[i]=u->uc_mcontext.regs[i];
#endif
    nso_module *m=fault_module;
    fprintf(stderr,"[fault] signal=%d pc=0x%" PRIx64 " fa=0x%" PRIx64,sig,pc,fa);
    if(m && range(pc-nso_base(m),1,m->image_size))fprintf(stderr," module_offset=0x%" PRIx64,pc-nso_base(m));
    fputc('\n',stderr);
    if(m) {
        int i=sentinel_index(m->import_traps,m->trap_size,pc);
        if(i<0)i=sentinel_index(m->object_traps,m->trap_size,fa);
        if(i>=0 && (uint32_t)i<m->sym_count) {
            fprintf(stderr,"[unresolved-import] index=%d name=%s",i,nso_symbol_name(m,(uint32_t)i));
            if(lr>=nso_base(m) && range(lr-nso_base(m),1,m->image_size))
                fprintf(stderr," caller_offset=0x%" PRIx64,lr-nso_base(m));
            else
                fprintf(stderr," caller_address=0x%" PRIx64 " caller_outside_module=true",lr);
            fputc('\n',stderr);
        }
    }
    for(int i=0;i<29;i++)fprintf(stderr," x%d=0x%" PRIx64 "%c",i,regs[i],i%4==3?'\n':' ');
    fprintf(stderr,"\n fp=0x%" PRIx64 " lr=0x%" PRIx64 " sp=0x%" PRIx64 "\n",fp,lr,sp);
    for(int depth=0;depth<16 && stack_hi>=16 && !(fp&15) && fp>=stack_lo && fp<=stack_hi-16;depth++) {
        const uint64_t *frame=(const uint64_t*)(uintptr_t)fp;
        uint64_t next=frame[0],ret=frame[1];
        fprintf(stderr," frame[%d] return=0x%" PRIx64,depth,ret);
        if(m && range(ret-nso_base(m),1,m->image_size))fprintf(stderr," module_offset=0x%" PRIx64,ret-nso_base(m));
        fputc('\n',stderr);
        if(next<=fp)break;
        fp=next;
    }
    fflush(stderr);
    _exit(128+sig);
}
int nso_fault_scope_end(nso_module *m) {
    if(!fault_module)return 0;
    if(fault_module!=m)return EINVAL;
    int error=0;
    for(int i=0;i<4;i++)if(owns_actions&(1u<<i)) {
        if(sigaction(fault_signals[i],&previous_actions[i],NULL)) {if(!error)error=errno;}
        else owns_actions&=~(1u<<i);
    }
    if(owns_signal_stack) {
        stack_t restore=previous_signal_stack;
        if((restore.ss_flags&SS_DISABLE) && restore.ss_size<MINSIGSTKSZ)restore.ss_size=MINSIGSTKSZ;
        if(sigaltstack(&restore,NULL)) {if(!error)error=errno;}
        else owns_signal_stack=0;
    }
    if(!error) {fault_module=NULL;stack_lo=stack_hi=0;}
    return error;
}
int nso_fault_scope_begin(nso_module *m) {
    if(!m || !m->ready || fault_module)return EBUSY;
    fault_module=m;
#if defined(__APPLE__)
    stack_hi=(uintptr_t)pthread_get_stackaddr_np(pthread_self());stack_lo=stack_hi-pthread_get_stacksize_np(pthread_self());
#elif defined(__linux__)
    pthread_attr_t attr;
    int e=pthread_getattr_np(pthread_self(),&attr);
    if(e) {fault_module=NULL;return e;}
    void *addr;size_t size;
    e=pthread_attr_getstack(&attr,&addr,&size);pthread_attr_destroy(&attr);
    if(e) {fault_module=NULL;return e;}
    stack_lo=(uintptr_t)addr;stack_hi=stack_lo+size;
#endif
    stack_t ss={.ss_sp=signal_stack,.ss_size=sizeof(signal_stack),.ss_flags=0};
    if(sigaltstack(&ss,&previous_signal_stack)) {int e=errno;fault_module=NULL;return e;}
    owns_signal_stack=1;
    struct sigaction sa;memset(&sa,0,sizeof(sa));sigemptyset(&sa.sa_mask);sa.sa_sigaction=fault_handler;sa.sa_flags=SA_SIGINFO|SA_ONSTACK;
    for(int i=0;i<4;i++) {
        if(sigaction(fault_signals[i],&sa,&previous_actions[i])) {
            int e=errno,cleanup=nso_fault_scope_end(m);
            return cleanup?cleanup:e;
        }
        owns_actions|=1u<<i;
    }
    return 0;
}
void nso_install_fault_trap(nso_module *m) {
    int e=nso_fault_scope_begin(m);
    if(e) {fprintf(stderr,"[fault-scope] installation failed errno=%d\n",e);abort();}
}
