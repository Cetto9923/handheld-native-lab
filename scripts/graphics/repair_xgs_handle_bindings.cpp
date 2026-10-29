/* Repair only XGS descriptor slot words using the translator's own OpName
 * handle identities and OpDecorate bindings. SPIR-V and constants are copied
 * byte-for-byte. Inputs remain read-only. Reject incomplete/unknown identity. */
#include <filesystem>
#include <fstream>
#include <vector>
#include <map>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdint>
namespace fs=std::filesystem;
static int table(uint32_t word,uint32_t base,uint32_t count){uint32_t at=word*4;return at>=base&&at<base+8*count&&(at-base)%8==0?int((at-base)/8):-1;}
static bool decode(const std::string &name,bool image,uint32_t &slot,uint32_t &sampler,uint32_t &source){
 auto cb=name.find("_cb"),tcb=name.find("_tcb_");unsigned banks=2,handle=0;int end=0;
 if(cb!=std::string::npos){if(cb<2||name[cb-1]!=(image?'i':'t')||std::sscanf(name.c_str()+cb+3,"%u_%x%n",&banks,&handle,&end)!=2||(cb+3+end!=name.size()&&name[cb+3+end]!='_')||(banks&65535)!=2)return false;}
 else if(tcb!=std::string::npos){if(image||std::sscanf(name.c_str()+tcb+5,"%x%n",&handle,&end)!=1||tcb+5+end!=name.size())return false;}
 else return false;
 uint32_t kind=handle>>28,w[2]={handle&0x3fff,(handle>>14)&0x3fff};if(kind>3)return false;unsigned n=kind==1||kind==2?2:1;int found=-1;sampler=UINT32_MAX;
 if(image){for(unsigned i=0;i<n&&found<0;i++)found=table(w[i],0x120,8);if(found<0)return false;slot=found;return true;}
 for(unsigned i=0;i<n&&found<0;i++){int x=table(w[i],0x168,128);if(x>=0)found=128+x;}
 if(found<0)for(unsigned i=0;i<n&&found<0;i++)found=table(w[i],0x20,32);
 if(found<0){for(unsigned i=0;i<n&&found<0;i++)found=table(w[i],0x120,8);if(found<0)return false;slot=found;source=8;return true;}
 if(kind==1||kind==2){for(unsigned i=0;i<n;i++){int x=table(w[i],0x568,128);if(x>=0){sampler=x;break;}}if(sampler==UINT32_MAX)return false;}
 slot=found;return true;
}
int main(int argc,char **argv){if(argc!=4)return 2;fs::path input=argv[1],output=argv[2];fs::create_directories(output);std::ofstream report(argv[3]);unsigned files=0,changed=0,failed=0;
 for(auto &entry:fs::directory_iterator(input)){if(entry.path().extension()!=".xgs")continue;files++;
 std::ifstream f(entry.path(),std::ios::binary|std::ios::ate);auto size=f.tellg();if(size<32)return 3;std::vector<uint32_t>b((size_t(size)+3)/4);f.seekg(0);f.read((char*)b.data(),size);auto original=b;
 auto bad=[&](const std::string &why){failed++;report<<"FAIL\t"<<entry.path().filename().string()<<"\t"<<why<<"\n";};
 if(b[0]!=0x31534758||b[1]!=1||b[3]>256){bad("header");continue;}size_t offset=8+8*b[3]+b[4]+b[5]/4;if(b[5]%4||offset+b[6]/4!=b.size()||b[offset]!=0x07230203){bad("extent");continue;}
 std::map<uint32_t,std::string> names;std::map<uint32_t,uint32_t> bindings;bool invalid=false;
 for(size_t i=offset+5;i<b.size();){uint32_t op=b[i]&65535,n=b[i]>>16;if(!n||i+n>b.size()){invalid=true;break;}if(op==5&&n>=3){const char *s=(char*)&b[i+2];size_t cap=(n-2)*4;const char *nul=(char*)std::memchr(s,0,cap);if(!nul){invalid=true;break;}names[b[i+1]]=std::string(s,nul-s);}else if(op==71&&n==4&&b[i+2]==33)bindings[b[i+3]]=b[i+1];i+=n;}
 if(invalid){bad("SPIRV grammar");continue;}
 for(unsigned i=0;i<b[3];i++){uint32_t *r=b.data()+8+8*i;if(r[4]!=7&&r[4]!=8)continue;auto id=bindings.find(r[0]);uint32_t slot=0,sampler=UINT32_MAX;
 if(id==bindings.end()||!decode(names[id->second],r[4]==8,slot,sampler,r[4])||r[2]>(r[4]==8?16u:256u)-slot){invalid=true;bad("descriptor identity: "+(id==bindings.end()?std::string("no binding"):names[id->second]));break;}r[5]=slot;r[6]=sampler;}
 if(invalid)continue;if(b!=original){changed++;std::ofstream out(output/entry.path().filename(),std::ios::binary);out.write((char*)b.data(),size);report<<"FIX\t"<<entry.path().filename().string()<<"\n";}
 if(files%5000==0)std::printf("scanned=%u changed=%u failed=%u\n",files,changed,failed),std::fflush(stdout);
 }
 std::printf("complete scanned=%u changed=%u failed=%u\n",files,changed,failed);return failed?1:0;
}
