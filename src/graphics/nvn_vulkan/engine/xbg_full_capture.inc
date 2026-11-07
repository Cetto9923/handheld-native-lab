// Included by xbg_probe.inc after ProbeState. Explicit Honor D/E only.
// All copies are recorded on the draw's command buffer. Independent destinations
// survive slot retirement; only present waits/invalidates/writes them to disk.
// No fence may retire transient descriptors or indirect arguments inside draw().
struct FullBufferRequest {
    Buffer source;uint64_t offset=0,bytes=0,extent=0;std::string role;bool writable=false;
};
struct FullImageRequest {
    std::shared_ptr<Image> owner;Image *image=nullptr;std::array<uint32_t,9> view{};
    VkImageView handle{};std::string role;bool writable=false;
};
struct FullReadback {
    std::shared_ptr<OwnedIndirectBuffer> owned;std::string label;size_t offset=0,bytes=0;unsigned event=0;
};
struct FullCapture {
    unsigned family=0,seen=0,selected=0,issued=0,missing=0,queued=0,written=0;bool target=false;
    uint64_t reserved=0;std::vector<FullBufferRequest> buffers;std::vector<FullImageRequest> images;
    std::vector<FullReadback> pending;
    std::map<std::vector<uint64_t>,std::string> saved_images;
} full_capture;
std::shared_ptr<Image> probe_keep(Image &im);
bool probe_full(){return full_capture.family && probe_is_active();}
bool probe_full_target(){return probe_full() && full_capture.target;}
unsigned full_shader_pair(const std::string &vs,const std::string &fs,unsigned family){
    if(family==1){
        if(vs=="db5f1e2dd1c9ef3d" && fs=="c0adc062e4b8e8fa")return 1;
        if(vs=="41a5a80e5242a26c" && fs=="b7dd0f48aea00196")return 2;
    }else if(family==2){
        if(vs=="085afbc955a0744f" && fs=="0066ad2ea935247d")return 1;
        if(vs=="632c207422d773c5" && fs=="17ab021c45c63390")return 2;
    }return 0;
}
bool probe_shader_requested(const std::string &key){
    const char *tablet=getenv("FFRS_PROFILE_TABLET");if(!tablet || tablet[0]!='1' || (!probe.waiting && !capture.full))return false;
    static const char *keys[]={"db5f1e2dd1c9ef3d","c0adc062e4b8e8fa","41a5a80e5242a26c","b7dd0f48aea00196",
        "085afbc955a0744f","0066ad2ea935247d","632c207422d773c5","17ab021c45c63390"};
    for(const char *k:keys)if(key==k)return true;return false;
}
void full_missing(const std::string &role,const char *reason){
    ++full_capture.missing;capture_log("FULL_MISSING event=%u role=%s reason=%s",capture.events,role.c_str(),reason);
}
bool full_reserve(uint64_t n,const std::string &role){
    constexpr uint64_t limit=256ull*1024*1024;
    auto &f=full_capture;
    if(!n || f.pending.size()>=2048 || capture.bytes>limit || f.reserved>limit-capture.bytes || n>limit-capture.bytes-f.reserved){full_missing(role,"bounded-capture-budget");return false;}
    f.reserved+=n;return true;
}
uint64_t full_buffer_extent(const Buffer &b){
    // allocation_size is VkMemory's bound, NOT the VkBuffer's legal extent.
    for(auto &s:active_slots())for(Arena *a:{&s.stream,&s.readback})for(auto &c:a->chunks)if(c.buffer==b.buffer)return c.size;
    for(auto &a:texel_arena)for(auto &c:a.chunks)if(c.buffer==b.buffer)return c.size;
    for(auto &c:static_arena.chunks)if(c.buffer==b.buffer)return c.size;
    return b.offset<=UINT64_MAX-b.size?b.offset+b.size:0; // conservative valid view end
}
void probe_full_buffer(const std::string &shader,unsigned stage,unsigned binding,unsigned element,
                       const Buffer &b,uint64_t offset,uint64_t bytes,VkFormat format,bool writable){
    if(!probe_full_target())return;
    std::string role="s"+std::to_string(stage)+"b"+std::to_string(binding)+"e"+std::to_string(element);
    capture_log("FULL_BUFFER_BIND event=%u shader=%s stage=%u binding=%u element=%u buffer=%p memory=%p offset=%llu range=%llu format=%u writable=%u slotGeneration=%llu role=%s",
        capture.events,shader.c_str(),stage,binding,element,(void*)b.buffer,(void*)b.memory,(unsigned long long)offset,(unsigned long long)bytes,unsigned(format),unsigned(writable),(unsigned long long)slot_serial,role.c_str());
    full_capture.buffers.push_back({b,offset,bytes,full_buffer_extent(b),role,writable});
}
void full_host_barrier(VkCommandBuffer cmd,const Buffer &b){
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=b.buffer;host.offset=0;host.size=b.size;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
}
void full_buffer_copy(VkCommandBuffer cmd,const FullBufferRequest &r,const char *phase){
    const std::string label="full-g"+std::to_string(capture.events)+"-"+phase+"-"+r.role;
    XbgCaptureBufferCopy plan{};
    if(!xbg_capture_buffer_copy(r.offset,r.bytes,r.extent,256ull*1024*1024,plan)){full_missing(label,"buffer-copy-range-or-alignment");return;}
    if(!full_reserve(plan.copy_bytes,label))return;
    auto dst=std::make_shared<OwnedIndirectBuffer>(size_t(plan.copy_bytes));
    VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};before.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_HOST_WRITE_BIT;before.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&before,0,nullptr,0,nullptr);
    VkBufferCopy copy{plan.source_offset,0,plan.copy_bytes};vkCmdCopyBuffer(cmd,r.source.buffer,dst->view.buffer,1,&copy);full_host_barrier(cmd,dst->view);
    // Subsequent shader/vertex/indirect accesses retain their original source.
    VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};after.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;after.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&after,0,nullptr,0,nullptr);
    capture_log("FULL_BUFFER_COPY event=%u phase=%s role=%s label=%s buffer=%p offset=%llu range=%llu actualExtent=%llu copyOffset=%llu copyBytes=%llu validOffset=%llu slotGeneration=%llu",
        capture.events,phase,r.role.c_str(),label.c_str(),(void*)r.source.buffer,(unsigned long long)r.offset,(unsigned long long)r.bytes,(unsigned long long)r.extent,(unsigned long long)plan.source_offset,(unsigned long long)plan.copy_bytes,(unsigned long long)plan.valid_offset,(unsigned long long)slot_serial);
    full_capture.pending.push_back({dst,label,size_t(plan.valid_offset),size_t(plan.valid_bytes),capture.events});++full_capture.queued;
}
bool full_image_request(Image &canonical,VkImageView view,const std::string &role,bool writable,FullImageRequest &out){
    Image *actual=&canonical;auto owner=probe_keep(canonical);
    auto found=actual->views.end();for(auto it=actual->views.begin();it!=actual->views.end();++it)if(it->second==view){found=it;break;}
    if(found==actual->views.end() && canonical.filtered){actual=canonical.filtered.get();found=actual->views.end();for(auto it=actual->views.begin();it!=actual->views.end();++it)if(it->second==view){found=it;break;}}
    if(!owner || found==actual->views.end()){full_missing(role,"actual-view-or-owner-not-found");return false;}
    out={owner,actual,found->first,view,role,writable};return true;
}
void probe_full_image(Image &im,VkImageView view,unsigned stage,unsigned binding,unsigned element,bool writable,const std::string &shader){
    if(!probe_full_target())return;FullImageRequest r;
    const std::string role="s"+std::to_string(stage)+"b"+std::to_string(binding)+"e"+std::to_string(element);
    if(!full_image_request(im,view,role,writable,r))return;
    const auto &k=r.view;Image &a=*r.image;
    capture_log("FULL_IMAGE_BIND event=%u shader=%s stage=%u binding=%u element=%u role=%s serial=%llu image=%p memory=%p view=%p format=%u viewFormat=%u viewType=%u aspects=%u mip=%u levels=%u layer=%u layers=%u swizzle=%#x usage=%u writable=%u version=%llu epoch=%llu writer=%llu site=%u",
        capture.events,shader.c_str(),stage,binding,element,role.c_str(),(unsigned long long)im.source.serial,(void*)a.image,(void*)a.memory,(void*)view,unsigned(a.fmt),k[4],k[5],k[6],k[0],k[1],k[2],k[3],k[8],k[7],unsigned(writable),(unsigned long long)a.content_version,(unsigned long long)a.provenance_epoch,(unsigned long long)a.provenance_writer,a.provenance_write_site);
    full_capture.images.push_back(std::move(r));
}
void full_image_copy(VkCommandBuffer cmd,const FullImageRequest &r,const char *phase){
    Image &im=*r.image;const auto &k=r.view;
    if(im.samples!=VK_SAMPLE_COUNT_1_BIT || im.layout==VK_IMAGE_LAYOUT_UNDEFINED || !(im.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT) || k[0]>=im.levels || !k[1] || k[1]>im.levels-k[0]){full_missing(r.role,"image-samples-layout-usage-or-mips");return;}
    const bool volume=im.create.imageType==VK_IMAGE_TYPE_3D;
    const bool slices=volume && k[5]!=VK_IMAGE_VIEW_TYPE_3D;
    if(!k[3] || (!volume && (k[2]>=im.layers || k[3]>im.layers-k[2])) || (volume && !slices && (k[2] || k[3]!=1))){full_missing(r.role,"image-layers");return;}
    for(uint32_t m=k[0];m<k[0]+k[1];++m){
        const uint32_t w=std::max(1u,im.width>>m),h=std::max(1u,im.height>>m),depth=std::max(1u,im.depth>>m);
        if(slices && (k[2]>=depth || k[3]>depth-k[2])){full_missing(r.role,"volume-slice-range");continue;}
        for(uint32_t l=k[2];l<k[2]+k[3];++l)for(uint32_t aspect:{1u,2u,4u})if(k[6]&aspect){
            XbgCaptureLayout layout{};uint32_t z=volume&&!slices?depth:1;
            std::string label="full-g"+std::to_string(capture.events)+"-"+phase+"-"+r.role+"-m"+std::to_string(m)+"-l"+std::to_string(l)+"-a"+std::to_string(aspect);
            if(!xbg_capture_layout(uint32_t(im.fmt),aspect,w,h,z,im.bpp,256ull*1024*1024,layout)){full_missing(label,"unsupported-or-overflow-layout");continue;}
            std::vector<uint64_t> identity{capture.events,uint64_t(im.image),im.source.serial,m,l,aspect,uint64_t(slices),z};
            // Phase is part of identity; attachments before/after are never
            // deduplicated by content_version (marked before create_pass).
            for(const char *p=phase;*p;++p)identity.push_back(uint8_t(*p));
            auto prior=full_capture.saved_images.find(identity);
            if(prior!=full_capture.saved_images.end()){capture_log("FULL_IMAGE_REF event=%u phase=%s role=%s mip=%u layer=%u aspect=%u label=%s",capture.events,phase,r.role.c_str(),m,l,aspect,prior->second.c_str());continue;}
            if(!full_reserve(layout.bytes,label))continue;
            auto dst=std::make_shared<OwnedIndirectBuffer>(size_t(layout.bytes));
            VkImageLayout saved=im.layout;im.transition(cmd,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkBufferImageCopy region{};region.imageSubresource={aspect,m,volume?0u:l,1};region.imageOffset.z=slices?int32_t(l):0;region.imageExtent={w,h,z};
            vkCmdCopyImageToBuffer(cmd,im.image,im.layout,dst->view.buffer,1,&region);full_host_barrier(cmd,dst->view);im.transition(cmd,saved);
            capture_log("FULL_IMAGE_COPY event=%u phase=%s role=%s label=%s serial=%llu image=%p format=%u viewFormat=%u samples=%u aspect=%u mip=%u layer=%u z=%d width=%u height=%u depth=%u block=%u/%u/%u rowBytes=%llu blockRows=%llu bytes=%llu layout=%u version=%llu writer=%llu slotGeneration=%llu",
                capture.events,phase,r.role.c_str(),label.c_str(),(unsigned long long)im.source.serial,(void*)im.image,unsigned(im.fmt),k[4],unsigned(im.samples),aspect,m,l,region.imageOffset.z,w,h,z,layout.block_width,layout.block_height,layout.block_bytes,(unsigned long long)layout.row_bytes,(unsigned long long)layout.block_rows,(unsigned long long)layout.bytes,unsigned(saved),(unsigned long long)im.content_version,(unsigned long long)im.provenance_writer,(unsigned long long)slot_serial);
            full_capture.pending.push_back({dst,label,0,size_t(layout.bytes),capture.events});full_capture.saved_images.emplace(std::move(identity),label);++full_capture.queued;
        }
    }
}
void full_attachments(VkCommandBuffer cmd,Pass &pass,const char *phase){
    for(int i=0;i<9;i++){
        const tex_t *t=i==8?state.rt.depth:(i<state.rt.n?state.rt.color[i]:nullptr);if(!t)continue;
        auto it=images.find(t->serial);if(it==images.end()){full_missing("attachment","image-not-found");continue;}Image &im=*it->second;
        if(std::find(pass.images.begin(),pass.images.end(),&im)==pass.images.end())continue;
        const bool has=i==8?state.rt.has_dview:state.rt.has_cview[i];const view_t *v=has?(i==8?&state.rt.dview:&state.rt.cview[i]):nullptr;
        FullImageRequest r;if(full_image_request(im,im.view(v,true),i==8?"depth-stencil":"color"+std::to_string(i),false,r))full_image_copy(cmd,r,phase);
    }
}
bool full_draw_begin(int count,unsigned instances,unsigned base_instance){
    if(!probe_full())return false;auto &f=full_capture;f.target=false;f.buffers.clear();f.images.clear();
    unsigned pair=state.shader[0] && state.shader[4]?full_shader_pair(state.shader[0]->key,state.shader[4]->key,f.family):0;
    if(!pair || (f.seen&pair) || f.selected>=2)return true;
    f.seen|=pair;++f.selected;f.target=true;++probe.groups;probe.draws=0;capture.events=probe.groups;
    capture_log("FULL_DRAW event=%u family=%u pair=%u frame=%llu submit=%llu globalDraw=%llu slotGeneration=%llu slot=%u command=%p count=%d instances=%u baseInstance=%u copiesBeforeDraw=1 copiesAfterDraw=1 waitAtPresentOnly=1",
        capture.events,f.family,pair,(unsigned long long)capture.frame,(unsigned long long)(stats.submissions.load()+1),(unsigned long long)(stats.draws.load()+1),(unsigned long long)slot_serial,slot_index,(void*)slots[slot_index].cmd,count,instances,base_instance);
    for(unsigned s=0;s<5;s++)if(state.shader[s]){const Shader &shader=*state.shader[s];
        std::string label="full-g"+std::to_string(capture.events)+"-shader-s"+std::to_string(s);
        capture_log("FULL_SHADER event=%u stage=%u key=%s module=%p words=%zu label=%s source=exact-vkCreateShaderModule-input",capture.events,s,shader.key.c_str(),(void*)shader.module,shader.probe_words.size(),label.c_str());
        if(shader.probe_words.empty())full_missing(label,"actual-module-bytes-not-retained");
        else{capture.active=true;capture_data(label.c_str(),shader.probe_words.data(),shader.probe_words.size()*4);capture.active=false;}
    }return false; // existing native state logger also records this target
}
void full_before_draw(VkCommandBuffer cmd,Pass &pass){
    if(!probe_full_target())return;++full_capture.issued;end_open_pass();
    capture_log("FULL_PASS event=%u pass=%p framebuffer=%p width=%u height=%u layers=%u samples=%u attachmentCount=%zu diagnosticBreak=store-load-copy canonicalState=original-api-stream",capture.events,(void*)pass.pass,(void*)pass.framebuffer,pass.width,pass.height,pass.layers,unsigned(pass.samples),pass.images.size());
    for(const auto &r:full_capture.buffers)full_buffer_copy(cmd,r,"input");
    for(const auto &r:full_capture.images)full_image_copy(cmd,r,"input");
    full_attachments(cmd,pass,"before");
}
void full_after_draw(Pass &pass){
    if(!probe_full_target())return;end_open_pass();VkCommandBuffer cmd=slots[slot_index].cmd;
    full_attachments(cmd,pass,"after");
    for(const auto &r:full_capture.buffers)if(r.writable)full_buffer_copy(cmd,r,"after-storage");
    for(const auto &r:full_capture.images)if(r.writable)full_image_copy(cmd,r,"after-storage");
    full_capture.target=false;full_capture.buffers.clear();full_capture.images.clear();
}
void full_present(Image &present){
    auto &f=full_capture;f.target=false;
    // Present hasn't acquired Commands/transient descriptors yet.
    if(!f.pending.empty()){
        end_open_pass();gpu_sync(perf.sync_readback);unsigned saved_event=capture.events;
        for(auto &r:f.pending){r.owned->view.invalidate();capture.events=r.event;unsigned errors=capture.errors;
            // A capture layer may protect mapped shadow pages to track CPU reads.
            // fwrite can hand those pages directly to write(2), which cannot
            // service a userspace page-guard fault. Read after the existing fence
            // and invalidate into ordinary heap memory before crossing into I/O.
            std::vector<uint8_t> bytes(r.bytes);
            memcpy(bytes.data(),static_cast<const uint8_t*>(r.owned->view.mapped)+r.offset,r.bytes);
            capture.active=true;capture_data(r.label.c_str(),bytes.data(),bytes.size());capture.active=false;
            if(errors==capture.errors)++f.written;
        }capture.events=saved_event;f.pending.clear();f.reserved=0;
    }
    if(f.seen!=3)full_missing("pair","pre-or-color-not-selected");
    if(ferror(capture.trace)){++capture.errors;clearerr(capture.trace);}
    capture_log("FULL_END family=%u selected=%u issued=%u pairMask=%u queued=%u written=%u missing=%u ioErrors=%u nativeReadbackComplete=%u replayAgreement=NOT_CHECKED presentedSerial=%llu",
        f.family,f.selected,f.issued,f.seen,f.queued,f.written,f.missing,capture.errors,unsigned(f.seen==3 && f.issued==2 && f.queued && f.queued==f.written && !f.missing && !capture.errors),(unsigned long long)present.source.serial);
}
