// Default-off, one-frame diagnostic. Included after Pass/Descriptors definitions.
// Readbacks occur only before acquiring the next draw/dispatch's transient state.
struct ProbeSurface {
    std::shared_ptr<Image> image;view_t view{};bool has_view=false;std::string role;
};
struct ProbeTexelSnapshot {
    std::shared_ptr<OwnedIndirectBuffer> owned;
    std::string shader;
    unsigned stage,binding,element,group,draw;
    VkFormat format;
    uint64_t guest,full_bytes;
    size_t bytes;
    bool resident;
};
struct ProbeQuerySnapshot {VkQueryPool pool;uint32_t index;unsigned group,draw;};
struct ProbeState {
    bool waiting=false,lut=false;std::string base,group;unsigned groups=0,draws=0,snapshots=0;uint64_t extra_buffer_bytes=0;
    std::vector<ProbeSurface> outputs,inputs;std::map<std::vector<uint64_t>,std::string> saved;
    std::vector<ProbeTexelSnapshot> texels;
    std::vector<ProbeQuerySnapshot> queries;
} probe;
bool probe_is_active(){return capture.probe && capture.trace && stats.presents.load()+1==capture.frame;}
void probe_draw_occlusion(){
    if(!probe_is_active() || !state.shader[4] || state.shader[4]->key!="f62f4204ee4db0da")return;
    capture_log("PROBE_QUERY_RECORDED group=%u draw=%u active=%u countSamples=%u batch=%u index=%u",probe.groups,probe.draws,unsigned(open_occlusion.cmd!=VK_NULL_HANDLE),unsigned(state.count_samples),unsigned(occlusion_batch_enabled),open_occlusion.index);
    if(open_occlusion.cmd && probe.queries.size()<64)probe.queries.push_back({open_occlusion.pool,open_occlusion.index,probe.groups,probe.draws});
}
void probe_flush_queries(){
    if(probe.queries.empty())return;
    // Read before any probe image command can reuse a slot and reset its pools.
    // Observe existing queries only; a batched value is not a per-draw count.
    end_open_pass();gpu_sync(perf.sync_readback);
    for(const auto &q:probe.queries){
        uint64_t result[2]{};VkResult rc=vkGetQueryPoolResults(cfg.device,q.pool,q.index,1,sizeof(result),result,sizeof(result),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        capture_log("PROBE_QUERY_RESULT group=%u draw=%u index=%u result=%d available=%llu samples=%llu",q.group,q.draw,q.index,int(rc),(unsigned long long)result[1],(unsigned long long)result[0]);
    }
    probe.queries.clear();
}
void probe_texel_buffer(const std::string &shader,unsigned stage,unsigned binding,unsigned element,
                        const Buffer &source,VkFormat format,uint64_t guest,uint64_t full_bytes,bool resident){
    const bool geometry=binding==12 || binding==18 || binding==24 || binding==30 || binding==36 || binding==42 || binding==48;
    if(!probe_is_active() || shader!="b0284572b5f180f2" || stage!=0 || !geometry || element!=0)return;
    const size_t bytes=size_t(std::min<uint64_t>(full_bytes,65536))&~size_t(3);
    if(!source.buffer || (source.offset&3) || !bytes || bytes>source.size ||
       probe.extra_buffer_bytes>8ull*1024*1024 || bytes>8ull*1024*1024-probe.extra_buffer_bytes){
        capture_log("PROBE_TEXEL_SKIP group=%u shader=%s binding=%u bytes=%llu sourceBytes=%zu",probe.groups,shader.c_str(),binding,(unsigned long long)full_bytes,source.size);return;
    }
    // Record only: submitting/retiring here would invalidate draw/descriptor
    // references. Independent storage survives the later slot retirement.
    auto owned=std::make_shared<OwnedIndirectBuffer>(bytes);
    Slot &slot=slots[slot_index];need(slot.recording,"Texel probe requires a recording command buffer");
    end_open_pass();
    VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    before.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;before.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(slot.cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&before,0,nullptr,0,nullptr);
    VkBufferCopy copy{source.offset,owned->view.offset,bytes};
    vkCmdCopyBuffer(slot.cmd,source.buffer,owned->view.buffer,1,&copy);
    VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    after.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;after.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(slot.cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,nullptr,0,nullptr);
    probe.extra_buffer_bytes+=bytes;
    probe.texels.push_back({std::move(owned),shader,stage,binding,element,probe.groups,probe.draws,format,guest,full_bytes,bytes,resident});
}
void probe_flush_texels(){
    if(probe.texels.empty())return;
    end_open_pass();gpu_sync(perf.sync_readback);
    for(auto &t:probe.texels){
        t.owned->view.invalidate();char label[160];
        snprintf(label,sizeof(label),"probe-texel-g%u-d%u-s%u-b%u-e%u",t.group,t.draw,t.stage,t.binding,t.element);
        capture_log("PROBE_TEXEL_DATA group=%u draw=%u shader=%s stage=%u binding=%u element=%u vk=%u guest=%#llx fullBytes=%llu capturedBytes=%zu resident=%u storage=%u label=%s",t.group,t.draw,t.shader.c_str(),t.stage,t.binding,t.element,unsigned(t.format),(unsigned long long)t.guest,(unsigned long long)t.full_bytes,t.bytes,unsigned(t.resident),unsigned(t.binding==12),label);
        capture.active=true;capture_data(label,t.owned->view.mapped,t.bytes);capture.active=false;
    }
    probe.texels.clear(); // Copy fence signalled before allocation destruction.
}
void probe_image_read_barrier(const Image &im,bool in_open,VkImageAspectFlags read_aspects){
    static unsigned logged=0;if(!attachment_barrier_enabled || !probe.waiting || capture.trace || !in_open || logged>=120)return;
    FILE *file=fopen((probe.base+"/gfx-codex-probe").c_str(),"rb");int value=file?fgetc(file):'0';if(file)fclose(file);if(value!='3')return;
    bool depth=open_pass.depth_attached && !open_pass.images.empty() && open_pass.images[open_pass.images.size()-1]==&im;
    VkStencilOpState f=stencil(false),b=stencil(true);++logged;
    xbg_logf("CODEX_IMAGE_READ frame=%llu op=%u image=%p serial=%llu vk=%u aspects=%#x readAspects=%#x depthAlias=%u prevAnyDSMayWrite=%u prevDepthMayWrite=%u prevStencilMayWrite=%u nextDT=%u nextDW=%u nextST=%u front=%u/%u/%u/%#x back=%u/%u/%u/%#x attachments=%zu fs=%s last_write=%llu barrier_seq=%llu",(unsigned long long)stats.presents.load(),cur_op,(const void*)im.image,(unsigned long long)im.source.serial,unsigned(im.fmt),unsigned(im.aspects),unsigned(read_aspects),unsigned(depth),unsigned(open_pass.depth_may_write),unsigned(open_pass.depth_writes),unsigned(open_pass.stencil_writes),unsigned(state.ds.dt),unsigned(state.ds.dw),unsigned(state.ds.st),unsigned(f.failOp),unsigned(f.passOp),unsigned(f.depthFailOp),f.writeMask,unsigned(b.failOp),unsigned(b.passOp),unsigned(b.depthFailOp),b.writeMask,open_pass.images.size(),state.shader[4]?state.shader[4]->key.c_str():"none",(unsigned long long)im.last_write,(unsigned long long)barrier_seq);
}
void probe_guest_barrier(uint32_t bits){
    static unsigned logged=0;if(!probe.waiting || capture.trace || !open_pass.cmd || logged>=120)return;
    FILE *file=fopen((probe.base+"/gfx-codex-probe").c_str(),"rb");int value=file?fgetc(file):'0';if(file)fclose(file);if(value!='3')return;
    ++logged;xbg_logf("CODEX_BARRIER_FLAGS frame=%llu bits=%#x attachments=%zu depth=%u stateColors=%d vs=%s fs=%s write_seq=%llu barrier_seq=%llu",(unsigned long long)stats.presents.load(),bits,open_pass.images.size(),unsigned(open_pass.depth_attached),state.rt.n,state.shader[0]?state.shader[0]->key.c_str():"none",state.shader[4]?state.shader[4]->key.c_str():"none",(unsigned long long)write_seq,(unsigned long long)barrier_seq);
}
void probe_cpu_sync(uint64_t address,uint64_t bytes,unsigned why,uint64_t pending_address,uint64_t pending_bytes){
    // Keep early CPU overwrites from consuming the storage-read diagnostic budget.
    // At most 12 records per access reason, enabled only by the existing marker 3.
    static unsigned logged[12]={};if(!probe.waiting || capture.trace || why>=12 || logged[why]>=12)return;
    FILE *file=fopen((probe.base+"/gfx-codex-probe").c_str(),"rb");int value=file?fgetc(file):'0';if(file)fclose(file);if(value!='3')return;
    ++logged[why];xbg_logf("CODEX_CPU_SYNC frame=%llu op=%u why=%u address=%#llx bytes=%llu pending=%#llx pending_bytes=%llu covers=%u cs=%s vs=%s fs=%s",(unsigned long long)stats.presents.load(),cur_op,why,(unsigned long long)address,(unsigned long long)bytes,(unsigned long long)pending_address,(unsigned long long)pending_bytes,unsigned(address<=pending_address && address+bytes>=pending_address+pending_bytes),state.shader[5]?state.shader[5]->key.c_str():"none",state.shader[0]?state.shader[0]->key.c_str():"none",state.shader[4]?state.shader[4]->key.c_str():"none");
    for(auto w=pending_writebacks.rbegin();w!=pending_writebacks.rend();++w)if(w->addr==pending_address && w->bytes==pending_bytes){const Slot *s=static_cast<const Slot*>(w->slot);int producer=int(s-slots);int done=s->pending?int(vkGetFenceStatus(cfg.device,s->fence)==VK_SUCCESS):-1;
        xbg_logf("CODEX_CPU_PRODUCER frame=%llu op=%u why=%u currentSlot=%u producerSlot=%d recording=%u pending=%u fenceDone=%d covers=%llu commands=%llu offset=%llu bytes=%llu",(unsigned long long)stats.presents.load(),cur_op,why,slot_index,producer,unsigned(s->recording),unsigned(s->pending),done,(unsigned long long)s->covers,(unsigned long long)s->ops,(unsigned long long)w->buffer.offset,(unsigned long long)w->buffer.size);break;}
    static bool guard_logged=false;if(why==5 && cur_op==27 && state.shader[5] && !guard_logged){
        guard_logged=true;xbg_logf("CODEX_INDIRECT_GUARD frame=%llu currentSlot=%u sourceCurrent=%u inputsResident=%u xviewGuard=%u",(unsigned long long)stats.presents.load(),slot_index,unsigned(current_pending_range(address,bytes,4)),unsigned(indirect_dispatch_inputs_resident()),unsigned(xview_guard));
        auto row=[](unsigned binding,uint64_t a,uint64_t n){
            int producer=-1;uint64_t wb=0,wbbytes=0,offset=0;for(auto w=pending_writebacks.rbegin();w!=pending_writebacks.rend();++w)if(xbg_wb_overlaps(a,n,w->addr,w->bytes)){producer=int(static_cast<const Slot*>(w->slot)-slots);wb=w->addr;wbbytes=w->bytes;offset=w->buffer.offset;break;}
            xbg_logf("CODEX_INDIRECT_BUFFER binding=%u address=%#llx bytes=%llu producerSlot=%d pending=%#llx pending_bytes=%llu bufferOffset=%llu",binding,(unsigned long long)a,(unsigned long long)n,producer,(unsigned long long)wb,(unsigned long long)wbbytes,(unsigned long long)offset);
        };
        row(0,address,bytes);for(auto &b:state.shader[5]->bindings)if((b.source==XBG_SOURCE_STORAGE || b.source==XBG_SOURCE_UNIFORM) && b.slot<16){auto r=b.source==XBG_SOURCE_STORAGE?state.sb[nvn_stage[5]][b.slot]:state.ub[nvn_stage[5]][b.slot];row(b.binding,r.address,r.size);}
    }
}
std::shared_ptr<Image> probe_keep(Image &im){for(auto &entry:images)if(entry.second.get()==&im)return entry.second;return {};}
void probe_add(std::vector<ProbeSurface> &to,Image &im,const view_t *view,const std::string &role){
    for(auto &old:to)if(old.image.get()==&im && old.has_view==bool(view) && (!view || !memcmp(&old.view,view,sizeof(*view))))return;
    auto keep=probe_keep(im);if(keep)to.push_back({std::move(keep),view?*view:view_t{},view!=nullptr,role});
}
void probe_stencil(Image &im,uint32_t mip,uint32_t layer){
    if(!(im.aspects&VK_IMAGE_ASPECT_STENCIL_BIT))return;
    const uint32_t w=std::max(1u,im.width>>mip),h=std::max(1u,im.height>>mip);
    Buffer buffer=readback_buffer(size_t(w)*h);Commands command;VkImageLayout saved=im.layout;
    im.transition(command.command,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkBufferImageCopy region{};region.bufferOffset=buffer.offset;region.imageSubresource={VK_IMAGE_ASPECT_STENCIL_BIT,mip,layer,1};region.imageExtent={w,h,1};
    vkCmdCopyImageToBuffer(command.command,im.image,im.layout,buffer.buffer,1,&region);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=buffer.buffer;host.offset=buffer.offset;host.size=buffer.size;
    vkCmdPipelineBarrier(command.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);im.transition(command.command,saved);command.finish();gpu_sync(perf.sync_readback);buffer.invalidate();
    const uint8_t *raw=static_cast<const uint8_t*>(buffer.mapped);uint64_t counts[256]{};for(size_t i=0;i<size_t(w)*h;i++)++counts[raw[i]];
    const uint32_t x=w/3,y=h/2,rw=std::max(1u,w/3),rh=std::max(1u,h/2);std::vector<uint8_t> roi(size_t(rw)*rh);
    for(uint32_t row=0;row<rh;row++)memcpy(roi.data()+size_t(row)*rw,raw+size_t(y+row)*w+x,rw);
    char label[128];snprintf(label,sizeof(label),"probe-stencil-g%u-n%u-s%llu-m%u",probe.groups,probe.snapshots,(unsigned long long)im.source.serial,mip);
    capture_log("PROBE_STENCIL group=%u serial=%llu label=%s width=%u height=%u x=%u y=%u roiWidth=%u roiHeight=%u",probe.groups,(unsigned long long)im.source.serial,label,w,h,x,y,rw,rh);
    for(unsigned value=0;value<256;value++)if(counts[value])capture_log("PROBE_STENCIL_HIST group=%u value=%u count=%llu",probe.groups,value,(unsigned long long)counts[value]);
    capture.active=true;capture_data(label,roi.data(),roi.size());capture.active=false;
}
void probe_snapshot(const ProbeSurface &surface){
    Image &im=*surface.image;const view_t *v=surface.has_view?&surface.view:nullptr;
    const uint32_t mip=v && v->set_levels?v->base_level:0,layer=v && v->set_layers?v->min_layer:0;
    if(mip>=im.levels || layer>=im.layers || im.samples!=VK_SAMPLE_COUNT_1_BIT || im.layout==VK_IMAGE_LAYOUT_UNDEFINED){capture_log("PROBE_SKIP role=%s reason=uninitialized-or-range",surface.role.c_str());return;}
    std::vector<uint64_t> key{uint64_t(im.image),im.content_version,mip,layer};
    auto prior=probe.saved.find(key);if(prior!=probe.saved.end()){capture_log("PROBE_REF group=%u role=%s serial=%llu image=%p version=%llu fileLabel=%s",probe.groups,surface.role.c_str(),(unsigned long long)im.source.serial,(void*)im.image,(unsigned long long)im.content_version,prior->second.c_str());return;}
    if(probe.snapshots>=1024 && surface.role!="present"){capture_log("PROBE_SKIP role=%s reason=1024-snapshot-limit",surface.role.c_str());return;}
    const uint32_t w=std::max(1u,im.width>>mip),h=std::max(1u,im.height>>mip),z=std::max(1u,im.depth>>mip);
    VkImageAspectFlags plane=(im.aspects&VK_IMAGE_ASPECT_DEPTH_BIT)?VK_IMAGE_ASPECT_DEPTH_BIT:im.aspects;
    uint32_t bpp=im.bpp;if(plane==VK_IMAGE_ASPECT_DEPTH_BIT){xbg_depth_format f;if(xbg_depth_format_info(im.source.d.format,&f))return;bpp=im.source.d.format==0x32 && depth16_as_d24?4:f.depth_bytes;}
    const uint64_t bytes=uint64_t(w)*h*z*bpp;
    if(!bpp || !bytes || bytes>capture_limit || plane==0){capture_log("PROBE_SKIP role=%s reason=extent bytes=%llu",surface.role.c_str(),(unsigned long long)bytes);return;}
    Buffer buffer=readback_buffer(size_t(bytes));Commands command;VkImageLayout saved=im.layout;
    im.transition(command.command,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkBufferImageCopy region{};
    region.bufferOffset=buffer.offset;region.imageSubresource={plane,mip,layer,1};region.imageExtent={w,h,z};
    vkCmdCopyImageToBuffer(command.command,im.image,im.layout,buffer.buffer,1,&region);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=buffer.buffer;host.offset=buffer.offset;host.size=buffer.size;
    vkCmdPipelineBarrier(command.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
    im.transition(command.command,saved);command.finish();gpu_sync(perf.sync_readback);buffer.invalidate();
    const uint32_t nx=std::min(w,128u),ny=std::min(h,72u),nz=std::min(z,32u);
    std::vector<uint8_t> sample(size_t(nx)*ny*nz*bpp);const uint8_t *raw=static_cast<const uint8_t*>(buffer.mapped);
    for(uint32_t k=0;k<nz;k++)for(uint32_t y=0;y<ny;y++)for(uint32_t x=0;x<nx;x++){
        const uint32_t sx=nx>1?uint64_t(x)*(w-1)/(nx-1):0,sy=ny>1?uint64_t(y)*(h-1)/(ny-1):0,sz=nz>1?uint64_t(k)*(z-1)/(nz-1):0;
        memcpy(sample.data()+((size_t(k)*ny+y)*nx+x)*bpp,raw+((size_t(sz)*h+sy)*w+sx)*bpp,bpp);
    }
    size_t nonzero=0;for(uint8_t byte:sample)nonzero+=byte!=0;
    char label[160];snprintf(label,sizeof(label),"probe-g%u-n%u-%s-s%llu",probe.groups,++probe.snapshots,surface.role.c_str(),(unsigned long long)im.source.serial);
    capture_log("PROBE_PIXELS group=%u role=%s label=%s serial=%llu image=%p guest=%p guestBytes=%llu nvnFormat=%#x vk=%u viewVk=%u aspect=%u mip=%u layer=%u width=%u height=%u depth=%u sampleWidth=%u sampleHeight=%u sampleDepth=%u bpp=%u version=%llu layout=%u nonzeroBytes=%zu bytes=%zu",probe.groups,surface.role.c_str(),label,(unsigned long long)im.source.serial,(void*)im.image,im.source.mem,(unsigned long long)im.source.L.size,im.source.d.format,unsigned(im.fmt),unsigned(im.view_format(v)),plane,mip,layer,w,h,z,nx,ny,nz,bpp,(unsigned long long)im.content_version,unsigned(im.layout),nonzero,sample.size());
    capture.active=true;capture_data(label,sample.data(),sample.size());capture.active=false;
    // The two diffuse/normal sprite atlases are static CPU uploads, so the
    // regular GPU-input sampling omits them. Preserve their exact texels only
    // in this explicit one-frame probe; never alter the draw or its bindings.
    if((surface.role=="spriteAtlas10" || surface.role=="spriteAtlas22") && z==1 && bytes<=1024*1024){
        std::string full_label=std::string(label)+"-full";
        capture_log("PROBE_SPRITE_ATLAS group=%u serial=%llu label=%s width=%u height=%u vk=%u bytes=%llu",probe.groups,(unsigned long long)im.source.serial,full_label.c_str(),w,h,unsigned(im.fmt),(unsigned long long)bytes);
        capture.active=true;capture_data(full_label.c_str(),raw,size_t(bytes));capture.active=false;
    }
    if(z==1 && w>=1000 && h>=500 && (surface.role=="color0" || surface.role=="color3")){
        const uint32_t x=w/3,y=h/2,rw=w/3,rh=h/2;std::vector<uint8_t> roi(size_t(rw)*rh*bpp);
        for(uint32_t row=0;row<rh;row++)memcpy(roi.data()+size_t(row)*rw*bpp,raw+(size_t(y+row)*w+x)*bpp,size_t(rw)*bpp);
        std::string roi_label=std::string(label)+"-roi";capture_log("PROBE_ROI group=%u role=%s serial=%llu label=%s vk=%u x=%u y=%u width=%u height=%u bpp=%u",probe.groups,surface.role.c_str(),(unsigned long long)im.source.serial,roi_label.c_str(),unsigned(im.fmt),x,y,rw,rh,bpp);
        capture.active=true;capture_data(roi_label.c_str(),roi.data(),roi.size());capture.active=false;
    }
    if(plane==VK_IMAGE_ASPECT_DEPTH_BIT)probe_stencil(im,mip,layer);
    probe.saved.emplace(std::move(key),label);
}
void probe_flush(){
    if(!probe_is_active())return;
    capture_log("PROBE_GROUP_END group=%u draws=%u outputs=%zu inputs=%zu",probe.groups,probe.draws,probe.outputs.size(),probe.inputs.size());
    probe_flush_queries();
    probe_flush_texels();
    for(auto &s:probe.outputs)probe_snapshot(s);for(auto &s:probe.inputs)probe_snapshot(s);
    probe.outputs.clear();probe.inputs.clear();probe.draws=0;
}
void probe_boundary(const char *kind){if(!probe_is_active())return;probe_flush();probe.group.clear();capture_log("PROBE_OP kind=%s",kind);}
void probe_start(uint64_t frame,bool lut);
void probe_draw_begin(int count,unsigned instances,unsigned base_instance){
    if(probe.waiting){for(int i=0;i<state.rt.n;i++)if(state.rt.color[i] && state.rt.color[i]->d.target==2){
        FILE *f=fopen((probe.base+"/gfx-codex-probe").c_str(),"rb");int c=f?fgetc(f):'0';if(f)fclose(f);
        if(c=='2')probe_start(stats.presents.load()+1,true);break;
    }}
    if(!probe_is_active())return;
    std::string key="draw:"+(state.shader[0]?state.shader[0]->key:"none")+":"+(state.shader[4]?state.shader[4]->key:"none");
    for(int i=0;i<state.rt.n;i++)key+=":"+std::to_string(state.rt.color[i]?state.rt.color[i]->serial:0);
    key+=":depth:"+std::to_string(state.rt.depth?state.rt.depth->serial:0);
    if(key!=probe.group){probe_flush();probe.group=key;++probe.groups;capture.events=probe.groups;capture_log("PROBE_GROUP group=%u kind=draw vs=%s fs=%s colors=%d depth=%llu mask=%#x depthTest=%u depthWrite=%u depthFunc=%u viewport=%g,%g,%g,%g",probe.groups,state.shader[0]?state.shader[0]->key.c_str():"none",state.shader[4]?state.shader[4]->key.c_str():"none",state.rt.n,(unsigned long long)(state.rt.depth?state.rt.depth->serial:0),state.mask.mask,unsigned(state.ds.dt),unsigned(state.ds.dw),unsigned(state.ds.df),state.viewport.x,state.viewport.y,state.viewport.width,state.viewport.height);}
    capture_log("PROBE_COLOR_STATE group=%u draw=%u blendEnable=%#x logicOp=%u alphaFunc=%u constants=%g,%g,%g,%g",probe.groups,probe.draws+1,unsigned(state.color.blend_en),unsigned(state.color.logic_op),unsigned(state.color.alpha_func),state.blend_color[0],state.blend_color[1],state.blend_color[2],state.blend_color[3]);
    for(int i=0;i<state.rt.n;i++){
        const blend_st &b=state.blend[i];xbg_draw2d converted{};
        uint32_t mask=state.shader[4]?((state.mask.mask>>(4*i))&15):0;
        int rc=xbg_2d_blend_from_nvn(&converted,(state.color.blend_en>>i)&1,b.src_rgb,b.dst_rgb,b.src_a,b.dst_a,b.eq_rgb,b.eq_a,mask);
        const auto &v=converted.blend;
        capture_log("PROBE_BLEND group=%u draw=%u target=%d enabled=%u nvnRGB=%u/%u/%u nvnAlpha=%u/%u/%u mask=%#x conversion=%d vkRGB=%u/%u/%u vkAlpha=%u/%u/%u",probe.groups,probe.draws+1,i,unsigned((state.color.blend_en>>i)&1),unsigned(b.src_rgb),unsigned(b.dst_rgb),unsigned(b.eq_rgb),unsigned(b.src_a),unsigned(b.dst_a),unsigned(b.eq_a),mask,rc,unsigned(v.srcColorBlendFactor),unsigned(v.dstColorBlendFactor),unsigned(v.colorBlendOp),unsigned(v.srcAlphaBlendFactor),unsigned(v.dstAlphaBlendFactor),unsigned(v.alphaBlendOp));
    }
    ++probe.draws;capture_log("PROBE_DRAW group=%u count=%d instances=%u baseInstance=%u gs=%s stencil=%u sf=%u/%u ref=%u/%u cmpMask=%#x/%#x writeMask=%#x/%#x opsF=%u/%u/%u opsB=%u/%u/%u",probe.groups,count,instances,base_instance,state.shader[3]?state.shader[3]->key.c_str():"none",unsigned(state.ds.st),unsigned(state.ds.sf_f),unsigned(state.ds.sf_b),state.stencil_ref[0],state.stencil_ref[1],state.stencil_compare[0],state.stencil_compare[1],state.stencil_write[0],state.stencil_write[1],unsigned(state.ds.fail_f),unsigned(state.ds.zfail_f),unsigned(state.ds.zpass_f),unsigned(state.ds.fail_b),unsigned(state.ds.zfail_b),unsigned(state.ds.zpass_b));
    capture_log("PROBE_RASTER group=%u draw=%u discard=%u tf=%u cull=%u front=%u polyMode=%u depthClamp=%u depthRange=%g/%g depthBounds=%u/%g/%g scissor=%d/%d/%u/%u swizzle=%u/%u/%u/%u conditionalSkip=%u",probe.groups,probe.draws,unsigned(state.rast_discard),unsigned(state.tf_on),unsigned(state.poly.cull),unsigned(state.poly.front),unsigned(state.poly.mode),unsigned(state.depth_clamp),state.viewport.minDepth,state.viewport.maxDepth,unsigned(state.bounds.enable),state.bounds.minimum,state.bounds.maximum,state.scissor.offset.x,state.scissor.offset.y,state.scissor.extent.width,state.scissor.extent.height,unsigned(state.vp_sw[0][0]),unsigned(state.vp_sw[0][1]),unsigned(state.vp_sw[0][2]),unsigned(state.vp_sw[0][3]),unsigned(state.render_cond_skip));
}
void probe_draw_end(Pass &pass){
    if(!probe_is_active())return;
    if(probe.lut)capture_log("PROBE_FRAMEBUFFER group=%u layers=%u width=%u height=%u",probe.groups,pass.layers,pass.width,pass.height);
    for(int i=0;i<9;i++){const tex_t *t=i==8?state.rt.depth:(i<state.rt.n?state.rt.color[i]:nullptr);if(!t)continue;
        auto found=images.find(t->serial);if(found==images.end())continue;Image &im=*found->second;
        if(std::find(pass.images.begin(),pass.images.end(),&im)==pass.images.end())continue;
        const bool has=i==8?state.rt.has_dview:state.rt.has_cview[i];const view_t *v=has?(i==8?&state.rt.dview:&state.rt.cview[i]):nullptr;
        if(probe.lut)capture_log("PROBE_TARGET group=%u index=%d serial=%llu target=%u depth=%u mip=%u baseLayer=%u layerCount=%u",probe.groups,i,(unsigned long long)t->serial,t->d.target,t->d.depth,v&&v->set_levels?v->base_level:0,v&&v->set_layers?v->min_layer:0,v&&v->set_layers?v->nlayers:(t->d.target==2?t->d.depth:t->L.layers));
        probe_add(probe.outputs,im,v,i==8?"depth":("color"+std::to_string(i)));
    }
}
void probe_buffer(const std::string &shader,unsigned stage,unsigned binding,unsigned element,const void *data,size_t bytes){
    if(!probe_is_active())return;
    const bool effect=shader=="f62f4204ee4db0da" || shader=="9b0acd9875f7c44a" || shader=="5fe1d12402b04134" || shader=="b0284572b5f180f2" || shader=="1e222af40b26bd25" || shader=="9528f530e1c95ce5" || shader=="c2ca757a9f517e96" || shader=="6018ff2b44b763d7";
    const bool sprite=shader=="b5e9ec0ba0af7aaa" || shader=="18a37643faef3594";
    if(!probe.lut && shader!="062df44b75d37d76"){
        if((!effect && !sprite) || bytes>65536 || bytes>8ull*1024*1024-probe.extra_buffer_bytes)return;
        // GPU-resident storage is not necessarily coherent in the CPU mapping.
        // Capture only the already-owned uniform/support snapshots here.
        if(!state.shader[stage])return;
        for(const auto &b:state.shader[stage]->bindings)if(b.binding==binding && b.type==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)return;
        probe.extra_buffer_bytes+=bytes;
    }
    char label[160];snprintf(label,sizeof(label),"probe-ub-g%u-d%u-s%u-b%u-e%u",probe.groups,probe.draws,stage,binding,element);
    capture_log("PROBE_BUFFER group=%u shader=%s stage=%u binding=%u element=%u bytes=%zu label=%s",probe.groups,shader.c_str(),stage,binding,element,bytes,label);
    capture.active=true;capture_data(label,data,bytes);capture.active=false;
}
void probe_dispatch_begin(){
    if(!probe_is_active())return;probe_flush();probe.group="dispatch";++probe.groups;capture.events=probe.groups;
    capture_log("PROBE_GROUP group=%u kind=dispatch cs=%s",probe.groups,state.shader[5]?state.shader[5]->key.c_str():"none");
}
bool probe_cpu_template_enabled(){
    if(!probe.waiting || capture.trace)return false;FILE *f=fopen((probe.base+"/gfx-codex-probe").c_str(),"rb");int v=f?fgetc(f):'0';if(f)fclose(f);return v=='3';
}
void probe_indirect_template(const p_dispatch &p){
    static unsigned logged=0;if(logged>=2 || !probe.waiting || !state.shader[5] || state.shader[5]->key!="9bee7f09d6c655c3" || !probe_cpu_template_enabled())return;
    auto clean=[](uint64_t a,uint64_t n){if(!a || !n || a+n<a)return false;for(auto &w:pending_writebacks)if(xbg_wb_overlaps(a,n,w.addr,w.bytes))return false;for(auto &r:pending_reports)if(xbg_wb_overlaps(a,n,r.addr,sizeof(NVNcounterData)))return false;return true;};
    const tex_t *input=nullptr,*output=nullptr;uint64_t ua=0,un=0;
    for(auto &b:state.shader[5]->bindings){
        if(b.binding==41 && b.source==XBG_SOURCE_UNIFORM && b.slot<16){auto r=state.ub[nvn_stage[5]][b.slot];ua=r.address;un=r.size;}
        if((b.binding!=11 && b.binding!=29) || b.count!=1 || !state.texture_pool.pool)continue;
        if((b.source==XBG_SOURCE_TEXTURE && b.slot>=256) || (b.source==XBG_SOURCE_IMAGE && b.slot>=16))continue;
        if(b.source!=XBG_SOURCE_TEXTURE && b.source!=XBG_SOURCE_IMAGE)continue;
        uint64_t h=b.source==XBG_SOURCE_TEXTURE?state.tex[nvn_stage[5]][b.slot]:state.img[nvn_stage[5]][b.slot];int id=int(h&0xfffff);auto pool=textures.find(pool_key(&state.texture_pool));auto &table=pool==textures.end()?legacy_textures:pool->second;auto t=table.find(id);if(t==table.end())continue;
        if(b.binding==11)input=&t->second.texture;else output=&t->second.texture;
    }
    ++logged;bool uc=un>=16 && clean(ua,16),tc=input && clean(uint64_t(input->mem),input->L.size);
    uint32_t u[4]{};if(uc)memcpy(u,reinterpret_cast<const void*>(ua),16);
    xbg_logf("CODEX_INDIRECT_TEMPLATE frame=%llu uniform=%#llx uniformClean=%u c3=%u/%u/%u/%u input=%p inputBytes=%llu inputClean=%u output=%p outputBytes=%llu groups=%d/%d/%d",(unsigned long long)stats.presents.load(),(unsigned long long)ua,unsigned(uc),u[0],u[1],u[2],u[3],input?input->mem:nullptr,(unsigned long long)(input?input->L.size:0),unsigned(tc),output?output->mem:nullptr,(unsigned long long)(output?output->L.size:0),p.x,p.y,p.z);
    if(!uc || !tc || !output || input->d.target!=10 || input->d.format!=0xb)return;
    for(uint64_t j=0;j<std::min<uint64_t>(u[1],256);j++){uint64_t k=(uint64_t(u[0])+j)*20;if(k+20>input->L.size)break;uint32_t q[5];memcpy(q,static_cast<const uint8_t*>(input->mem)+k,20);
        xbg_logf("CODEX_INDIRECT_TEMPLATE_ROW frame=%llu job=%llu wordOffset=%u argument=%#llx count=%u firstIndex=%u flags=%#x instanceSource=%u",(unsigned long long)stats.presents.load(),(unsigned long long)j,q[0],(unsigned long long)(uint64_t(output->mem)+uint64_t(q[0])*4),q[2],q[3],q[4],q[1]);}
}
void probe_indirect_arguments(const p_draw_idx &p,const uint32_t *a){
    static unsigned logged=0;if(logged>=100 || !probe_cpu_template_enabled())return;++logged;
    xbg_logf("CODEX_INDIRECT_ARGUMENT frame=%llu address=%#llx count=%u instances=%u firstIndex=%u baseVertexBits=%u baseInstance=%u index=%#llx indexType=%d prim=%d",(unsigned long long)stats.presents.load(),(unsigned long long)p.ind,a[0],a[1],a[2],a[3],a[4],(unsigned long long)p.ibuf,p.itype,p.prim);
}
void probe_bind(Image &im,const view_t *v,bool storage,uint32_t stage,uint32_t binding,VkImageView image_view,const std::string &shader){
    if((probe.waiting || probe_is_active()) && storage && im.source.d.format==0x27 && im.depth>1)
        xbg_logf("CODEX_VOLUME_WRITER shader=%s stage=%u binding=%u serial=%llu guest=%p extent=%ux%ux%u viewVk=%u",shader.c_str(),stage,binding,(unsigned long long)im.source.serial,im.source.mem,im.width,im.height,im.depth,unsigned(im.view_format(v)));
    if(!probe_is_active())return;
    if(shader=="062df44b75d37d76" && binding==58 && im.source.mem && im.source.L.size<=65536){
        capture_log("PROBE_LAST_WRITE serial=%llu site=%u sequence=%llu uploaded=%u gpu=%u cpuHash=%llu",(unsigned long long)im.source.serial,im.provenance_write_site,(unsigned long long)im.provenance_writer,unsigned(im.uploaded),unsigned(im.gpu_written),(unsigned long long)im.cpu_hash);
        const std::string label="probe-cpu-g"+std::to_string(probe.groups)+"-s"+std::to_string(im.source.serial);
        capture_log("PROBE_CPU_TEXTURE group=%u binding=%u serial=%llu bytes=%llu label=%s",probe.groups,binding,(unsigned long long)im.source.serial,(unsigned long long)im.source.L.size,label.c_str());
        capture.active=true;capture_data(label.c_str(),im.source.mem,size_t(im.source.L.size));capture.active=false;
    }
    capture_log("PROBE_VIEW_MODE group=%u shader=%s binding=%u serial=%llu setDs=%u ds=%u texStencil=%u setSwz=%u swz=%u/%u/%u/%u texSetSwz=%u texSwz=%#x",probe.groups,shader.c_str(),binding,(unsigned long long)im.source.serial,unsigned(v&&v->set_dsmode),v?unsigned(v->dsmode):0,unsigned(im.source.stencil),unsigned(v&&v->set_swizzle),v?unsigned(v->swz[0]):0,v?unsigned(v->swz[1]):0,v?unsigned(v->swz[2]):0,v?unsigned(v->swz[3]):0,unsigned(im.source.set_swz),unsigned(im.source.swz));
    capture_log("PROBE_VIEW_ASPECT group=%u binding=%u sampledAspect=%u",probe.groups,binding,unsigned(im.sampled_aspects(v)));
    capture_log("PROBE_BIND group=%u shader=%s stage=%u binding=%u storage=%u serial=%llu image=%p view=%p guest=%p guestBytes=%llu nvnFormat=%#x vk=%u viewVk=%u layout=%u mip=%u levels=%u layer=%u layers=%u gpuWritten=%u rg16Proxy=%u filteredImage=%p",probe.groups,shader.c_str(),stage,binding,unsigned(storage),(unsigned long long)im.source.serial,(void*)im.image,(void*)image_view,im.source.mem,(unsigned long long)im.source.L.size,im.source.d.format,unsigned(im.fmt),unsigned(im.view_format(v, !storage)),unsigned(im.layout),v&&v->set_levels?v->base_level:0,v&&v->set_levels?v->nlevels:im.levels,v&&v->set_layers?v->min_layer:0,v&&v->set_layers?v->nlayers:im.layers,unsigned(im.gpu_written),unsigned(im.rg16_proxy),(void*)(im.filtered?im.filtered->image:VK_NULL_HANDLE));
    if(!storage && shader=="b5e9ec0ba0af7aaa" && (binding==10 || binding==22))probe_add(probe.inputs,im,v,"spriteAtlas"+std::to_string(binding));
    else if(storage)probe_add(probe.outputs,im,v,"storage"+std::to_string(binding));
    else if(im.gpu_written || im.depth>1)probe_add(probe.inputs,im,v,"input"+std::to_string(stage)+"b"+std::to_string(binding));
}
void probe_volume_copy(Image &im,uint64_t source,size_t bytes,size_t row,size_t stride){
    if((!probe.waiting && !probe_is_active()) || im.source.d.format!=0x27 || im.depth<2 || bytes>65536)return;
    const auto *p=reinterpret_cast<const uint8_t *>(source);size_t nonzero=0;unsigned amin=255,amax=0;
    for(size_t i=0;i<bytes;i++){nonzero+=p[i]!=0;if((i&3)==3){amin=std::min(amin,unsigned(p[i]));amax=std::max(amax,unsigned(p[i]));}}
    xbg_logf("CODEX_VOLUME_COPY serial=%llu src=%p bytes=%zu row=%zu imageStride=%zu sourceNonzero=%zu alpha=%u/%u first=%02x%02x%02x%02x beforeSite=%u uploaded=%u gpu=%u",(unsigned long long)im.source.serial,(void*)source,bytes,row,stride,nonzero,amin,amax,bytes>0?p[0]:0,bytes>1?p[1]:0,bytes>2?p[2]:0,bytes>3?p[3]:0,im.provenance_write_site,unsigned(im.uploaded),unsigned(im.gpu_written));
}
void probe_init(){
    std::string dir=cfg.shader_directory;size_t cut=dir.find_last_of('/');probe.base=cut==std::string::npos?dir:dir.substr(0,cut);
    FILE *file=fopen((probe.base+"/gfx-codex-probe").c_str(),"rb");probe.waiting=file!=nullptr;if(file)fclose(file);
    xbg_logf("CODEX_PROBE enabled=%d marker=gfx-codex-probe (0 waits; 1 captures next complete frame)",int(probe.waiting));
}
void probe_start(uint64_t frame,bool lut){
    const std::string directory=probe.base+"/codex-probe-"+std::to_string(getpid());
    if(mkdir(directory.c_str(),0755) && errno!=EEXIST){xbg_logf("CODEX_PROBE mkdir failed errno=%d",errno);probe.waiting=false;return;}
    FILE *trace=fopen((directory+"/capture.txt").c_str(),"wb");if(!trace){probe.waiting=false;return;}
    probe.waiting=false;probe.lut=lut;probe.group.clear();probe.groups=probe.draws=probe.snapshots=0;probe.extra_buffer_bytes=0;probe.outputs.clear();probe.inputs.clear();probe.saved.clear();
    capture.directory=directory;capture.trace=trace;capture.frame=frame;capture.bytes=0;capture.events=0;capture.active=false;capture.probe=true;
    capture_log("PROBE version=3 frame=%llu lutProducer=%u rawReadback=full sampleGridMax=128x72x32 maxSnapshots=1024 maxFileBytes=%llu",(unsigned long long)capture.frame,unsigned(lut),(unsigned long long)capture_limit);
    xbg_logf("CODEX_PROBE armed frame=%llu directory=%s lutProducer=%u",(unsigned long long)capture.frame,directory.c_str(),unsigned(lut));
}
void probe_present(Image &present){
    if(probe_is_active()){probe_flush();ProbeSurface final{probe_keep(present),{},false,"present"};if(final.image)probe_snapshot(final);return;}
    if(!probe.waiting || capture.trace)return;
    FILE *file=fopen((probe.base+"/gfx-codex-probe").c_str(),"rb");int value=file?fgetc(file):'0';if(file)fclose(file);
    if(value=='4'){
        // Diagnose the normal frame after recording, without disabling its
        // descriptor/indirect fast paths or reading every intermediate pass.
        probe_start(stats.presents.load()+1,false);
        if(!capture.trace)return;
        capture.probe=false;
        capture_log("FINAL_ONLY normalFrameRecording=1");
        ProbeSurface final{probe_keep(present),{},false,"present"};
        if(final.image)probe_snapshot(final);
        return;
    }
    if(value!='1')return;
    probe_start(stats.presents.load()+2,false);
}
