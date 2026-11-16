"""Exercise production program indices and immutable bridge-cache hits."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / 'src/graphics/nvn_device'


class ProgramLookup(unittest.TestCase):
    def run_fixture(self, fixture):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.c').write_text(fixture)
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-pthread', '-I', str(NATIVE),
                            str(path / 'test.c'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_live_policy_and_missing_host_hook(self):
        source = (ROOT / 'src/graphics/nvn_vulkan/engine/xbg_game.cpp').read_text()
        start = source.index('void update_program_lookup_policy(){')
        helper = source[start:source.index('void memo_reference_buffer(', start)]
        # The native mode is applied before a live-switch acknowledgement.
        start = source.index('strncpy(ab_force,tk,')
        switch = source[start:source.index('}}}', start)]
        self.assertLess(switch.index('update_program_lookup_policy();'),
                        switch.index('xbg_logf("LIVE-AB'))
        start = source.index('void perf_frame(){')
        frame = source[start:source.index('    pipelines_unsaved+=', start)]
        self.assertLess(frame.index('ab_frame_on=ab_enabled();'),
                        frame.index('update_program_lookup_policy();'))
        fixture = r'''
#include <cassert>
#include <cstring>
bool forced=false;
int mode=-1;
bool ab_off(const char *name){assert(!strcmp(name,"progcache"));return forced;}
void setter(int enabled){mode=enabled;}
void (*nvn_program_set_fast_lookup)(int)=nullptr;
''' + helper + r'''
int main(){
    update_program_lookup_policy();assert(mode==-1);
    nvn_program_set_fast_lookup=setter;
    update_program_lookup_policy();assert(mode==1);
    forced=true;update_program_lookup_policy();assert(mode==0);
    forced=false;update_program_lookup_policy();assert(mode==1);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'test.cpp').write_text(fixture)
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', str(path / 'test.cpp'),
                            '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)

    def test_program_indices_growth_reuse_and_serial_reads(self):
        source = (NATIVE / 'nvn_program.c').read_text()
        definitions = source[source.index('#define NVN_PROGRAM_MAX_STAGES '):
                             source.index('/* Take one stage over from the guest.')]
        getters = source[source.index('int nvn_program_serial_of('):
                         source.index('/* The names ')]
        fixture = r'''
#include "nvn_program.h"
#include "nvn_obj_ffrs.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fail_growth;
static void *fixture_realloc(void *memory,size_t bytes) {
    if(fail_growth){fail_growth=0;return NULL;}
    return realloc(memory,bytes);
}
#define realloc fixture_realloc
#include "nvn_object_table.h"
#undef realloc
''' + definitions + getters + r'''
static uintptr_t identities[5000];
static void initialize_record(unsigned index) {
    pthread_mutex_lock(&s_program_lock);
    nvn_program_state *entry=program_find(&identities[index],1);
    assert(entry);
    entry->serial=++s_program_serial;
    entry->stage_count=2;
    for(unsigned stage=0;stage<6;++stage){
        entry->stages[stage].data_address=entry->serial*16+stage;
        memset(entry->stages[stage].control,(unsigned char)entry->serial,0x880);
    }
    pthread_mutex_unlock(&s_program_lock);
}
static void check_info(const nvn_program_info *info) {
    assert(info->nstage==2 && info->serial);
    for(unsigned stage=0;stage<6;++stage){
        assert(info->sd[stage].data==info->serial*16+stage);
        assert(info->sd[stage].control==info->control[stage]);
        assert(info->control[stage][0]==(unsigned char)info->serial);
        assert(info->control[stage][0x87f]==(unsigned char)info->serial);
    }
}
static void *reader(void *unused) {
    (void)unused;
    for(unsigned index=0;index<4096;++index){
        uint64_t serial;
        nvn_program_info info;
        if(nvn_program_serial_of(&identities[index],&serial))assert(serial);
        if(nvn_program_info_of(&identities[index],&info))check_info(&info);
    }
    return NULL;
}
static void *writer(void *unused) {
    (void)unused;
    for(unsigned iteration=0;iteration<256;++iteration)initialize_record(0);
    return NULL;
}
int main(void) {
    (void)s_stats;
    uint64_t serial=777;
    assert(!nvn_program_serial_of(NULL,&serial) && serial==777);
    assert(!nvn_program_serial_of(&identities[0],NULL));
    fail_growth=1;
    assert(!program_find(&identities[0],1) && !s_programs.count);
    for(unsigned index=0;index<4096;++index){
        if(s_programs.count && s_programs.count==s_programs.capacity){
            fail_growth=1;
            assert(!program_find(&identities[index],1));
            assert(s_programs.count==index);
            assert(nvn_program_serial_of(&identities[0],&serial) && serial==1);
        }
        initialize_record(index);
        assert(nvn_program_serial_of(&identities[index],&serial) && serial==index+1);
        // A hot index remains valid after every possible backing realloc.
        assert(nvn_program_serial_of(&identities[0],&serial) && serial==1);
    }
    assert(!program_find(&identities[4999],1) && s_programs.count==4096);
    // More identities than hints force collisions. Both A/B modes see the
    // same full records and preserve all six control sections.
    for(int mode=0;mode<2;++mode){
        nvn_program_set_fast_lookup(mode);
        assert(nvn_program_fast_lookup_enabled()==mode);
        for(unsigned index=0;index<4096;++index){
            nvn_program_info info;
            assert(nvn_program_serial_of(&identities[index],&serial) && serial==index+1);
            assert(nvn_program_info_of(&identities[index],&info));check_info(&info);
        }
    }
    pthread_mutex_lock(&s_program_lock);
    program_find(&identities[0],0)->in_use=0;
    pthread_mutex_unlock(&s_program_lock);
    serial=777;assert(!nvn_program_serial_of(&identities[0],&serial) && serial==777);
    initialize_record(4096); // Reuse the freed slot for a different address.
    assert(!nvn_program_serial_of(&identities[0],&serial));
    pthread_mutex_lock(&s_program_lock);
    program_find(&identities[1],0)->in_use=0;
    pthread_mutex_unlock(&s_program_lock);
    initialize_record(0); // Recreate the original address at another slot.
    assert(nvn_program_serial_of(&identities[0],&serial) && serial>4096);
    assert(!nvn_program_serial_of(&identities[1],&serial));
    pthread_t threads[5];
    assert(!pthread_create(&threads[0],NULL,writer,NULL));
    for(unsigned index=1;index<5;++index)assert(!pthread_create(&threads[index],NULL,reader,NULL));
    for(unsigned index=0;index<5;++index)assert(!pthread_join(threads[index],NULL));
    nvn_object_table_free(&s_programs);
    assert(!nvn_program_serial_of(&identities[0],&serial));
    initialize_record(0);
    assert(nvn_program_serial_of(&identities[0],&serial));
    nvn_object_table_free(&s_programs);
}
'''
        self.run_fixture(fixture)

    def test_bridge_hits_avoid_control_copies_and_preserve_generations(self):
        source = (NATIVE / 'nvn_bridge.c').read_text()
        definitions = source[source.index('typedef struct bridge_snapshot'):
                             source.index('static pthread_mutex_t s_lock')]
        helpers = source[source.index('static bridge_entry *entry_find('):
                         source.index('/* Called with s_lock held;')]
        function = source[source.index('const ffrs_prog *nvn_bridge_program('):
                          source.index('const ffrs_pool *nvn_bridge_pool(')]
        fixture = r'''
#include "nvn_bridge.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
static unsigned char guest;
static int fast,live=1,replace_after_serial,finalize_after_serial;
static uint64_t generation=1,full_copies,serial_reads;
int nvn_program_fast_lookup_enabled(void) {return fast;}
int nvn_program_serial_of(const void *program,uint64_t *serial) {
    assert(program==&guest);++serial_reads;
    if(!live)return 0;
    *serial=generation;
    if(replace_after_serial){++generation;replace_after_serial=0;}
    if(finalize_after_serial){live=0;finalize_after_serial=0;}
    return 1;
}
int nvn_program_info_of(const void *program,nvn_program_info *out) {
    assert(program==&guest);
    if(!live)return 0;
    ++full_copies;memset(out,0,sizeof(*out));out->serial=generation;out->nstage=2;
    for(unsigned stage=0;stage<6;++stage){
        out->sd[stage].data=generation*16+stage;
        memset(out->control[stage],(unsigned char)generation,0x880);
        out->sd[stage].control=out->control[stage];
    }
    return 1;
}
''' + definitions + r'''
static pthread_mutex_t s_lock=PTHREAD_MUTEX_INITIALIZER;
static bridge_table s_programs;
static uint64_t s_objects,s_regenerated;
''' + helpers + function + r'''
static void check_snapshot(const ffrs_prog *program,uint64_t serial) {
    assert(program && program->serial==serial && program->nstage==2);
    for(unsigned stage=0;stage<6;++stage){
        const unsigned char *control=program->sd[stage].control;
        assert(control==(const unsigned char*)(program+1)+stage*0x880);
        assert(program->sd[stage].data==serial*16+stage);
        assert(control[0]==(unsigned char)serial && control[0x87f]==(unsigned char)serial);
    }
}
int main(void) {
    assert(!nvn_bridge_program(NULL));
    const ffrs_prog *first=nvn_bridge_program(&guest);check_snapshot(first,1);
    assert(full_copies==1 && serial_reads==0);
    fast=1;
    for(unsigned index=0;index<10000;++index)assert(nvn_bridge_program(&guest)==first);
    assert(full_copies==1 && serial_reads==10000);
    fast=0;assert(nvn_bridge_program(&guest)==first && full_copies==2);
    fast=1;replace_after_serial=1;
    assert(nvn_bridge_program(&guest)==first && generation==2 && full_copies==2);
    const ffrs_prog *second=nvn_bridge_program(&guest);check_snapshot(second,2);
    assert(first!=second && full_copies==3);check_snapshot(first,1);
    live=0;assert(!nvn_bridge_program(&guest));
    live=1;++generation;replace_after_serial=1;
    // A replacement between the scalar miss and full copy publishes the
    // newer complete snapshot. The previously recorded snapshots remain valid.
    const ffrs_prog *fourth=nvn_bridge_program(&guest);check_snapshot(fourth,4);
    check_snapshot(first,1);check_snapshot(second,2);
    ++generation;finalize_after_serial=1;
    assert(!nvn_bridge_program(&guest));check_snapshot(fourth,4);
    live=1;
    const ffrs_prog *fifth=nvn_bridge_program(&guest);check_snapshot(fifth,5);
    assert(full_copies==5 && s_objects==1 && s_regenerated==3);
    free((void*)first);free((void*)second);free((void*)fourth);free((void*)fifth);
    free(s_programs.entries);
}
'''
        self.run_fixture(fixture)


if __name__ == '__main__':
    unittest.main()
