#include <cassert>
#include <cstdio>
#include <vector>
#include "rsx_shader_spirv.h"
#include "depth_shaders.h"
struct Interface { std::vector<unsigned> inputs,outputs; };
static Interface check(const char* source,int stage)
{
    std::vector<u32> words(65536);u32 count=0;char log[8192]={};
    const int result=rsx_hlsl_to_spirv(source,stage,words.data(),(u32)words.size(),&count,log,sizeof log);
    if(result) fprintf(stderr,"stage=%d result=%d %s\n",stage,result,log);
    assert(result==0 && count>=5 && words[0]==0x07230203);
    std::vector<unsigned> storage(words[3],~0u),location(words[3],~0u);
    std::vector<bool> entry_interface(words[3],false);
    for(u32 cursor=5;cursor<count;) {
        const u32 size=words[cursor]>>16,op=words[cursor]&65535;
        assert(size && cursor+size<=count);
        if(op==59 && size>=4) storage.at(words[cursor+2])=words[cursor+3];
        if(op==71 && size>=4 && words[cursor+2]==30) location.at(words[cursor+1])=words[cursor+3];
        if(op==15) {
            u32 operand=cursor+3;
            while(operand<cursor+size) {
                const u32 name=words[operand++];
                if(!(name&255u)||!(name&0xff00u)||!(name&0xff0000u)||!(name&0xff000000u)) break;
            }
            while(operand<cursor+size) entry_interface.at(words[operand++])=true;
        }
        cursor+=size;
    }
    Interface reflection;
    for(unsigned id=0;id<storage.size();++id) if(entry_interface[id] && location[id]!=~0u) {
        if(storage[id]==1) {
            reflection.inputs.push_back(location[id]);
            if(stage==RSX_SHADER_STAGE_VERTEX) assert(location[id]==0);
        }
        if(storage[id]==3) reflection.outputs.push_back(location[id]);
    }
    if(stage==RSX_SHADER_STAGE_VERTEX) assert(reflection.inputs.size()==1);
    printf("Exact depth shader stage=%d words=%u entrypoint_user_inputs=%zu entrypoint_user_outputs=%zu.\n",stage,count,reflection.inputs.size(),reflection.outputs.size());
    fflush(stdout);
    return reflection;
}
int main()
{
    assert(rsx_hlsl_to_spirv_available());
    const auto vertex=check(kEngDepthAsColorVS,RSX_SHADER_STAGE_VERTEX);
    const auto fragment=check(kEngDepthAsColorPS,RSX_SHADER_STAGE_FRAGMENT);
    for(unsigned input:fragment.inputs) {
        bool matched=false;
        for(unsigned output:vertex.outputs) if(input==output) matched=true;
        if(!matched) fprintf(stderr,"Depth fragment entrypoint location %u has no vertex output.\n",input);
        assert(matched);
    }
}
