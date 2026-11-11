#include <cassert>
#include <cstdio>
#include <vector>
#include "rsx_shader_spirv.h"
#include "depth_shaders.h"
static void check(const char* source,int stage)
{
    std::vector<u32> words(65536);u32 count=0;char log[8192]={};
    const int result=rsx_hlsl_to_spirv(source,stage,words.data(),(u32)words.size(),&count,log,sizeof log);
    if(result) fprintf(stderr,"stage=%d result=%d %s\n",stage,result,log);
    assert(result==0 && count>=5 && words[0]==0x07230203);
    std::vector<unsigned> storage(words[3],~0u),location(words[3],~0u);
    for(u32 cursor=5;cursor<count;) {
        const u32 size=words[cursor]>>16,op=words[cursor]&65535;
        assert(size && cursor+size<=count);
        if(op==59 && size>=4) storage.at(words[cursor+2])=words[cursor+3];
        if(op==71 && size>=4 && words[cursor+2]==30) location.at(words[cursor+1])=words[cursor+3];
        cursor+=size;
    }
    unsigned located_inputs=0;
    for(unsigned id=0;id<storage.size();++id) if(storage[id]==1 && location[id]!=~0u) {
        ++located_inputs;
        if(stage==RSX_SHADER_STAGE_VERTEX) assert(location[id]==0);
    }
    if(stage==RSX_SHADER_STAGE_VERTEX) assert(located_inputs==1);
    printf("Exact depth shader stage=%d words=%u located_inputs=%u compiled through runtime Windows glslang.\n",stage,count,located_inputs);
}
int main()
{
    assert(rsx_hlsl_to_spirv_available());
    check(kEngDepthAsColorVS,RSX_SHADER_STAGE_VERTEX);
    check(kEngDepthAsColorPS,RSX_SHADER_STAGE_FRAGMENT);
}
