#include "../../src/graphics/nvn_vulkan/engine/xbg_clear.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
int main() {
    int errors=0;
    float rgba[4]={0,.25f,.5f,1};
    for(auto format:{VK_FORMAT_B8G8R8A8_UNORM,VK_FORMAT_B8G8R8A8_SRGB,VK_FORMAT_R8G8B8A8_SRGB}) {
        VkClearColorValue out{};
        if(xbg_clear_value(format,0,rgba,&out)!=0 || std::memcmp(out.float32,rgba,16))errors++;
    }
    for(unsigned f=0x99;f<=0x9c;f++)if(xbg_clear_color_class(f)!=0)errors++;
    VkClearColorValue out;std::memset(&out,0xa5,sizeof(out));VkClearColorValue original=out;
    if(xbg_clear_value(VK_FORMAT_UNDEFINED,0,rgba,&out)!=-1 || std::memcmp(&out,&original,sizeof(out)))errors++;
    std::printf("clear_format_contract errors=%d\n",errors);return errors?1:0;
}
