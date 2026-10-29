#include "../../src/graphics/nvn_device/nvn_bridge.h"
#include "../../src/graphics/nvn_device/nvn_object_table.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern int nvn_program_initialize(void*,void*);
extern int nvn_program_set_shaders(void*,int,const void*);
static unsigned char code[16];
int nvn_device_is_ready(const void *device){return device==(void*)1;}
void *nvn_memory_pool_resolve_address(uint64_t address,size_t *available){*available=sizeof code;return address==123?code:0;}
int main(void){int errors=0;nvn_object_table table={0,0,0,8};
 for(unsigned i=0;i<200;i++){uint64_t *p=nvn_object_table_new_entry(&table,256);if(!p)return 2;*p=i;if(table.capacity!=64&&table.capacity!=128&&table.capacity!=256)errors++;}
 for(unsigned i=0;i<200;i++)if(*(uint64_t*)nvn_object_table_at(&table,i)!=i)errors++;nvn_object_table_free(&table);
 unsigned char control[0x880];memset(control,0x31,sizeof control);uint32_t len=0x880,bytes=16;memcpy(control+0x20,&len,4);memcpy(control+0x704,&bytes,4);
 struct{uint64_t data;void *control;} sd={123,control};uint64_t keys[140]={0};
 if(!nvn_program_initialize(&keys[0],(void*)1)||!nvn_program_set_shaders(&keys[0],1,&sd))return 3;
 const ffrs_prog *snapshot=nvn_bridge_program(&keys[0]);if(!snapshot)return 4;uint64_t serial=snapshot->serial;
 for(int i=1;i<140;i++)if(!nvn_program_initialize(&keys[i],(void*)1))return 5;
 if(((const unsigned char*)snapshot->sd[0].control)[0]!=0x31||snapshot->serial!=serial)errors++;
 control[0]=0x57;nvn_program_set_shaders(&keys[0],1,&sd);const ffrs_prog *next=nvn_bridge_program(&keys[0]);
 if(!next||next==snapshot||next->serial==serial||((const unsigned char*)next->sd[0].control)[0]!=0x57||((const unsigned char*)snapshot->sd[0].control)[0]!=0x31)errors++;
 printf("program_snapshot_contract errors=%d table_growth=64/128/256 old_shader_preserved=1\n",errors);return errors!=0;}
