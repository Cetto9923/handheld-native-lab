// Targeted water-plane evidence. Copies stay in the existing recording slot and
// files are written only after its fence retires. Diagnostic pass breaks and
// transfers perturb scheduling, so this trace is not a performance measurement.
void water_binding_buffer(WaterBindings &out,const std::string &shader,unsigned stage,unsigned binding,unsigned element,
                          const Buffer &source,uint64_t offset,uint64_t bytes,VkFormat format,bool dynamic){
    if(!water_ready() || element)return;
    const bool uniform=format==VK_FORMAT_UNDEFINED;
    const bool vertex=stage==0 && ((uniform && binding==66) || (!uniform && (binding==18 || binding==24 || binding==30 || binding==36)));
    const bool fragment=stage==4 && uniform && (binding==4 || binding==64 || binding==70);
    if((vertex && shader=="431a6183c3565f66") || (fragment && shader=="6e2d000b284e669c"))
        out.buffers.push_back({source,offset,bytes,stage,binding,format,dynamic});
}
void water_binding_image(WaterBindings &out,const std::string &shader,unsigned stage,unsigned binding,unsigned element,Image &image,VkImageView view){
    if(!water_ready() || element)return;
    if((stage==4 && binding==22 && shader=="6e2d000b284e669c") ||
       (stage==5 && (binding==47 || binding==59 || binding==77) && shader=="f55cfedfb74f811a"))
        out.images.push_back({&image,view,stage,binding});
}
struct WaterTraceRun {
    FILE *data=nullptr,*index=nullptr;std::string directory;std::mutex io;
    uint64_t first=0,last=0,reserved=0,written=0,queued=0,saved=0,skipped=0,pass_breaks=0;bool failed=false,window_done=false;
    ~WaterTraceRun(){
        if(data && fclose(data))failed=true;
        if(index)fprintf(index,"END window_done=%u queued=%llu saved=%llu skipped=%llu pass_breaks=%llu bytes=%llu failed=%u\n",
            unsigned(window_done),(unsigned long long)queued,(unsigned long long)saved,(unsigned long long)skipped,
            (unsigned long long)pass_breaks,(unsigned long long)written,unsigned(failed));
        if(index && fclose(index))failed=true;
        xbg_logf("WATER_TRACE_COMPLETE directory=%s window_done=%u queued=%llu saved=%llu bytes=%llu failed=%u",
            directory.c_str(),unsigned(window_done),(unsigned long long)queued,(unsigned long long)saved,(unsigned long long)written,unsigned(failed));
    }
};
struct WaterTraceState {
    std::shared_ptr<WaterTraceRun> run;uint64_t poll=0,frame=~0ull,gpu_bytes=0;unsigned serial=0,draws=0,dispatches=0;
} water_trace;
const char *water_shader(unsigned stage){return stage==0?"431a6183c3565f66":stage==4?"6e2d000b284e669c":"f55cfedfb74f811a";}
void water_log(const std::shared_ptr<WaterTraceRun> &run,const char *format,...){
    std::lock_guard<std::mutex> lock(run->io);if(!run->index)return;
    va_list args;va_start(args,format);vfprintf(run->index,format,args);va_end(args);fputc('\n',run->index);
}
bool water_ready(){
    auto &trace=water_trace;if(!trace.run)return false;const uint64_t frame=stats.presents.load();
    if(frame<trace.run->first || frame>trace.run->last || trace.run->failed)return false;
    if(trace.frame!=frame){trace.frame=frame;trace.draws=trace.dispatches=0;trace.gpu_bytes=0;}
    return true;
}
void water_skip(const char *role,const char *reason){
    auto run=water_trace.run;if(!run)return;
    if(++run->skipped<=32)water_log(run,"SKIP frame_pre_increment=%llu role=%s reason=%s",(unsigned long long)stats.presents.load(),role,reason);
}
bool water_reserve(uint64_t output,uint64_t gpu,const char *role){
    auto &trace=water_trace;auto run=trace.run;
    constexpr uint64_t disk_limit=256ull*1024*1024,frame_gpu_limit=32ull*1024*1024;
    if(!output || output>disk_limit-run->reserved || gpu>frame_gpu_limit-trace.gpu_bytes){water_skip(role,"budget");return false;}
    run->reserved+=output;trace.gpu_bytes+=gpu;return true;
}
void water_end_pass(){if(end_open_pass())++water_trace.run->pass_breaks;}
void water_host_barrier(VkCommandBuffer cmd,const Buffer &out){
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=out.buffer;barrier.offset=out.offset;barrier.size=out.size;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
}
// One container per run. The index labels include pid/frame/shader/draw/role,
// physical/view formats and extent. Offsets refer to the bytes actually written.
void water_write(const std::shared_ptr<WaterTraceRun> &run,const std::string &label,const uint8_t *data,size_t expected){
    std::lock_guard<std::mutex> lock(run->io);if(run->failed)return;
    const uint64_t offset=run->written;const size_t bytes=fwrite(data,1,expected,run->data);
    if(bytes!=expected)run->failed=true;
    run->written+=bytes;++run->saved;
    if(fprintf(run->index,"DATA %s offset=%llu length=%zu expected=%zu\n",label.c_str(),(unsigned long long)offset,bytes,expected)<0)run->failed=true;
}
void water_trace_buffer(VkCommandBuffer cmd,const WaterBufferBinding &binding,unsigned draw){
    const Buffer &source=binding.source;XbgCaptureBufferCopy plan{};
    if(!source.buffer || binding.offset<source.offset || binding.offset-source.offset>source.size ||
       binding.bytes>source.size-(binding.offset-source.offset) || binding.bytes>65536 ||
       !xbg_capture_buffer_copy(binding.offset,binding.bytes,full_buffer_extent(source),65540,plan)){
        water_skip("buffer","range-or-alignment");return;
    }
    if(!water_reserve(plan.valid_bytes,plan.copy_bytes,"buffer"))return;
    water_end_pass();Slot &slot=slots[slot_index];Buffer out=readback_buffer(size_t(plan.copy_bytes));
    VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};before.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT;before.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&before,0,nullptr,0,nullptr);
    VkBufferCopy copy{plan.source_offset,out.offset,plan.copy_bytes};vkCmdCopyBuffer(cmd,source.buffer,out.buffer,1,&copy);water_host_barrier(cmd,out);
    VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};after.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;after.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&after,0,nullptr,0,nullptr);
    char label[512];snprintf(label,sizeof(label),"pid=%d frame_pre_increment=%llu shader=%s draw=%u role=s%ub%u vk=%u extent=%llux1 source_offset=%llu copy_offset=%llu copy_bytes=%llu valid_offset=%llu slot_serial=%llu",
        getpid(),(unsigned long long)stats.presents.load(),water_shader(binding.stage),draw,binding.stage,binding.binding,unsigned(binding.format),
        (unsigned long long)binding.bytes,(unsigned long long)binding.offset,(unsigned long long)plan.source_offset,
        (unsigned long long)plan.copy_bytes,(unsigned long long)plan.valid_offset,(unsigned long long)slot_serial);
    auto run=water_trace.run;++run->queued;
    slot.retire.handles.push_back([run,out,label=std::string(label),plan]() mutable {
        out.invalidate();water_write(run,label,static_cast<const uint8_t*>(out.mapped)+plan.valid_offset,size_t(plan.valid_bytes));
    });
}
struct WaterRoi {uint32_t x,y,w,h;uint64_t offset=0;};
void water_trace_image(VkCommandBuffer cmd,const WaterImageBinding &binding,unsigned draw,const char *role,bool full_depth=false,const view_t *target_view=nullptr){
    Image *image=binding.image;if(!image)return;
    std::array<uint32_t,9> view{0,1,0,1,uint32_t(image->fmt),uint32_t(VK_IMAGE_VIEW_TYPE_2D),uint32_t(image->aspects),0,0};
    if(binding.view){
        auto found=image->views.end();for(auto it=image->views.begin();it!=image->views.end();++it)if(it->second==binding.view){found=it;break;}
        if(found==image->views.end()){water_skip(role,"actual-view-not-found");return;}view=found->first;
    }else if(target_view){
        if(target_view->set_levels)view[0]=target_view->base_level;
        if(target_view->set_layers){view[2]=target_view->min_layer;view[3]=target_view->nlayers;}
        view[4]=uint32_t(image->view_format(target_view));
    }
    Image &im=*image;const uint32_t mip=view[0],layer=view[2],aspect=full_depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
    if(mip>=im.levels || layer>=im.layers || im.depth!=1 || im.samples!=VK_SAMPLE_COUNT_1_BIT ||
       !(im.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT) || im.layout==VK_IMAGE_LAYOUT_UNDEFINED || view[6]!=aspect ||
       (full_depth && !(im.aspects&VK_IMAGE_ASPECT_DEPTH_BIT)) || (!full_depth && im.aspects!=VK_IMAGE_ASPECT_COLOR_BIT)){
        water_skip(role,"image-subresource-layout-or-aspect");return;
    }
    const uint32_t w=std::max(1u,im.width>>mip),h=std::max(1u,im.height>>mip);
    WaterRoi roi[2]{};unsigned count=full_depth?1:2;const uint32_t rectangles[2][4]={{260,420,100,80},{900,490,150,90}};
    uint64_t bytes=0;uint32_t bpp=0;
    for(unsigned i=0;i<count;i++){
        const auto &r=rectangles[i];const uint32_t x0=uint64_t(r[0])*w/1280,y0=uint64_t(r[1])*h/720;
        const uint32_t x1=(uint64_t(r[0]+r[2])*w+1279)/1280,y1=(uint64_t(r[1]+r[3])*h+719)/720;
        bytes=(bytes+3)&~uint64_t(3);
        roi[i]=full_depth?WaterRoi{0,0,w,h,bytes}:WaterRoi{x0,y0,x1-x0,y1-y0,bytes};
        XbgCaptureLayout layout{};
        if(!xbg_capture_layout(im.fmt,aspect,roi[i].w,roi[i].h,1,im.bpp,8ull*1024*1024,layout) || layout.block_width!=1 || layout.block_height!=1){water_skip(role,"image-copy-format-or-size");return;}
        bpp=layout.block_bytes;bytes+=layout.bytes;
    }
    if(!water_reserve(bytes,bytes,role))return;
    water_end_pass();Slot &slot=slots[slot_index];Buffer out=readback_buffer(size_t(bytes));const VkImageLayout saved=im.layout;
    if(im.layout!=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)im.transition(cmd,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy copies[2]{};
    for(unsigned i=0;i<count;i++){auto &c=copies[i];c.bufferOffset=out.offset+roi[i].offset;c.imageSubresource={aspect,mip,layer,1};c.imageOffset={int32_t(roi[i].x),int32_t(roi[i].y),0};c.imageExtent={roi[i].w,roi[i].h,1};}
    vkCmdCopyImageToBuffer(cmd,im.image,im.layout,out.buffer,count,copies);water_host_barrier(cmd,out);
    if(im.layout!=saved)im.transition(cmd,saved);
    auto run=water_trace.run;const uint64_t frame=stats.presents.load(),serial=im.source.serial;const uint32_t format=im.fmt;
    run->queued+=count;
    slot.retire.handles.push_back([run,out,roi0=roi[0],roi1=roi[1],count,bpp,w,h,view,frame,serial,format,draw,role=std::string(role),full_depth,pid=getpid(),shader=std::string(binding.stage<6?water_shader(binding.stage):"present")]() mutable {
        out.invalidate();const WaterRoi regions[2]={roi0,roi1};
        for(unsigned i=0;i<count;i++){const auto &r=regions[i];char label[768];
            snprintf(label,sizeof(label),"pid=%d frame_pre_increment=%llu shader=%s draw=%u role=%s-%s vk=%u view_vk=%u aspect=%u mip=%u layer=%u view_layers=%u swizzle=%u serial=%llu extent=%ux%u roi=%u/%u/%u/%u bpp=%u raw_top_left=1 normalized_to=1280x720 full_depth=%u",
                pid,(unsigned long long)frame,shader.c_str(),draw,role.c_str(),full_depth?"full":i?"right":"left",format,view[4],full_depth?2u:1u,view[0],view[2],view[3],view[8],(unsigned long long)serial,w,h,r.x,r.y,r.w,r.h,bpp,unsigned(full_depth));
            water_write(run,label,static_cast<const uint8_t*>(out.mapped)+r.offset,size_t(r.w)*bpp*r.h);
        }
    });
}
unsigned water_trace_draw_begin(VkCommandBuffer cmd,const WaterBindings &bindings,int count,unsigned instances,unsigned base_instance,bool indirect){
    if(!water_ready() || !state.shader[0] || !state.shader[4] || state.shader[0]->key!="431a6183c3565f66" || state.shader[4]->key!="6e2d000b284e669c")return 0;
    if(++water_trace.draws>8){water_skip("draw","eight-draw-limit");return 0;}
    const unsigned draw=water_trace.draws;auto target=state.rt.color[0]?images.find(state.rt.color[0]->serial):images.end();
    const bool selected=target!=images.end() && target->second->fmt==VK_FORMAT_R16G16B16A16_SFLOAT;
    water_log(water_trace.run,"DRAW frame_pre_increment=%llu shader=431a6183c3565f66/6e2d000b284e669c draw=%u count=%d instances_cpu=%u base_instance=%u indirect=%u memo=%u selected_vk97=%u buffers=%zu images=%zu depth_test=%u depth_write=%u depth_func=%u",
        (unsigned long long)stats.presents.load(),draw,count,instances,base_instance,unsigned(indirect),unsigned(bindings.memo),unsigned(selected),bindings.buffers.size(),bindings.images.size(),unsigned(state.ds.dt),unsigned(state.ds.dw),unsigned(state.ds.df));
    if(!selected)return 0;
    for(const auto &binding:bindings.buffers)water_trace_buffer(cmd,binding,draw);
    for(const auto &binding:bindings.images)if(binding.stage==4 && binding.binding==22)water_trace_image(cmd,binding,draw,"fs-b22-depth",true);
    return draw;
}
void water_trace_draw_end(VkCommandBuffer cmd,unsigned draw,const VkViewport &viewport,const VkRect2D &scissor){
    if(!draw)return;auto found=images.find(state.rt.color[0]->serial);if(found==images.end())return;
    water_log(water_trace.run,"RASTER frame_pre_increment=%llu draw=%u viewport=%g/%g/%g/%g/%g/%g scissor=%d/%d/%u/%u cull=%u front=%u stencil=%u ref=%u/%u write_mask=%u/%u",
        (unsigned long long)stats.presents.load(),draw,viewport.x,viewport.y,viewport.width,viewport.height,viewport.minDepth,viewport.maxDepth,
        scissor.offset.x,scissor.offset.y,scissor.extent.width,scissor.extent.height,unsigned(state.poly.cull),unsigned(state.poly.front),unsigned(state.ds.st),state.stencil_ref[0],state.stencil_ref[1],state.stencil_write[0],state.stencil_write[1]);
    water_trace_image(cmd,{found->second.get(),VK_NULL_HANDLE,4,0},draw,"draw-color0",false,state.rt.has_cview[0]?&state.rt.cview[0]:nullptr);
}
unsigned water_trace_dispatch_begin(VkCommandBuffer cmd,const WaterBindings &bindings){
    if(!water_ready() || !state.shader[5] || state.shader[5]->key!="f55cfedfb74f811a")return 0;
    if(++water_trace.dispatches>4){water_skip("dispatch","four-dispatch-limit");return 0;}
    const unsigned draw=water_trace.dispatches;
    for(const auto &binding:bindings.images)if(binding.stage==5 && binding.binding!=77){const std::string role="cs-b"+std::to_string(binding.binding);water_trace_image(cmd,binding,draw,role.c_str());}
    return draw;
}
void water_trace_dispatch_end(VkCommandBuffer cmd,const WaterBindings &bindings,unsigned draw){
    if(draw)for(const auto &binding:bindings.images)if(binding.stage==5 && binding.binding==77)water_trace_image(cmd,binding,draw,"cs-output77");
}
void water_trace_present(Image &image,VkCommandBuffer cmd){
    if(!water_ready())return;auto run=water_trace.run;const uint64_t frame=stats.presents.load();
    water_trace_image(cmd,{&image,VK_NULL_HANDLE,6,0},0,"present");
    water_log(run,"FRAME frame_pre_increment=%llu counter_after_present=%llu draws=%u dispatches=%u gpu_copy_bytes=%llu pass_breaks_total=%llu",
        (unsigned long long)frame,(unsigned long long)(frame+1),water_trace.draws,water_trace.dispatches,
        (unsigned long long)water_trace.gpu_bytes,(unsigned long long)run->pass_breaks);
    if(frame==run->last){run->window_done=true;water_log(run,"WINDOW_DONE first=%llu last=%llu waiting_for_slot_retirement=1",(unsigned long long)run->first,(unsigned long long)run->last);water_trace.run.reset();}
}
void water_trace_tick(){
    auto &trace=water_trace;const uint64_t frame=stats.presents.load();
    if(trace.run && (trace.run->failed || frame>trace.run->last)){
        water_log(trace.run,"WINDOW_ABORT frame_pre_increment=%llu io_failed=%u",(unsigned long long)frame,unsigned(trace.run->failed));
        trace.run.reset();
    }
    if(trace.run || frame<trace.poll)return;trace.poll=frame+30;
    const std::string marker=probe.base+"/gfx-live-water-trace";FILE *file=fopen(marker.c_str(),"rb");if(!file)return;
    char request[32]{};const size_t length=fread(request,1,sizeof(request)-1,file);fclose(file);remove(marker.c_str());
    if(!length || request[0]<'0' || request[0]>'9')return;
    char *end=nullptr;errno=0;const unsigned long requested=strtoul(request,&end,10);
    while(*end && strchr(" \t\r\n",*end))++end;
    if(errno || *end || !requested)return;
    auto run=std::make_shared<WaterTraceRun>();run->first=frame+1;run->last=frame+std::min(requested,90ul);
    run->directory=probe.base+"/water-trace-p"+std::to_string(getpid())+"-f"+std::to_string(run->first)+"-r"+std::to_string(++trace.serial);
    if(mkdir(run->directory.c_str(),0755)){run->failed=true;return;}
    run->data=fopen((run->directory+"/samples.bin").c_str(),"wb");run->index=fopen((run->directory+"/index.txt").c_str(),"wb");
    if(!run->data || !run->index){run->failed=true;return;}
    setvbuf(run->data,nullptr,_IOFBF,1u<<20);setvbuf(run->index,nullptr,_IOFBF,65536);
    water_log(run,"WATER_TRACE version=1 pid=%d first=%llu last=%llu frame_number=stats.presents_before_increment first_probe_equivalent=%llu max_output_bytes=268435456 max_gpu_bytes_per_frame=33554432 left=260/420/100/80 right=900/490/150/90 normalized_to=1280x720 depth_capture=full_first_view_mip no_extra_submit=1 no_extra_wait=1 scheduling_perturbed=1",
        getpid(),(unsigned long long)run->first,(unsigned long long)run->last,(unsigned long long)(run->first+1));
    trace.run=run;trace.frame=~0ull;
    xbg_logf("WATER_TRACE_START first=%llu last=%llu directory=%s",(unsigned long long)run->first,(unsigned long long)run->last,run->directory.c_str());
}
