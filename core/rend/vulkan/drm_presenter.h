// EasyCast: process-local DRM presentation for the RG DS Plus Mali Vulkan ICD.
// Images are imported dma-bufs; no CPU pixel copies. Included only in Linux build.
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <dlfcn.h>
#include <chrono>
#include <stdexcept>

class EasycastDrmPresenter {
    void *lib = nullptr;
    int fd = -1;
    uint32_t connector = 0, crtc = 0;
    drmModeModeInfo mode{};
    drmModeCrtc saved{};
    bool modeset = false, pending = false;
public:
    bool foreignQueue = true;	// false: driver without VK_EXT_queue_family_foreign -> VK_QUEUE_FAMILY_EXTERNAL
private:
    VkDevice dev = VK_NULL_HANDLE;
    struct Buffer { uint32_t handle=0, fb=0; VkImage image=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; bool used=false; };
    std::vector<Buffer> buffers;
    template<class T> T fn(const char *name) {
        auto p = reinterpret_cast<T>(dlsym(lib, name));
        if (!p) throw std::runtime_error(std::string("Missing DRM entry: ")+name);
        return p;
    }
    static void check(VkResult result, const char *name) {
        if (result != VK_SUCCESS) throw std::runtime_error(std::string(name)+" result="+std::to_string(result));
    }
    static void flipped(int, unsigned, unsigned, unsigned, void *p) { *static_cast<bool*>(p)=false; }
    void waitFlip() {
        if (!pending) return;
        drmEventContext ec{}; ec.version=2; ec.page_flip_handler=flipped;
        auto handle=fn<int(*)(int,drmEventContext*)>("drmHandleEvent");
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(pending) {
            if(std::chrono::steady_clock::now()>until) throw std::runtime_error("DRM page flip timed out");
            pollfd p{fd,POLLIN,0}; int r=poll(&p,1,100);
            if(r<0 && errno==EINTR) continue;
            if(r<0 || (p.revents&(POLLERR|POLLHUP|POLLNVAL))) throw std::runtime_error("DRM poll failed");
            if(r>0 && handle(fd,&ec)) throw std::runtime_error("DRM event failed");
        }
    }
public:
    EasycastDrmPresenter(const char *screen) {
        try {
            lib=dlopen("libdrm.so.2",RTLD_NOW|RTLD_LOCAL);
            if(!lib) throw std::runtime_error("Cannot load libdrm");
            fd=open("/dev/dri/card0",O_RDWR|O_CLOEXEC);
            if(fd<0) throw std::runtime_error("Cannot open DRM card0");
            auto master=fn<int(*)(int)>("drmSetMaster");
            auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);
            while(master(fd)) {
                if(std::chrono::steady_clock::now()>until) throw std::runtime_error("DRM master busy before Vulkan init");
                usleep(50000);
            }
            auto getres=fn<drmModeRes*(*)(int)>("drmModeGetResources");
            auto freeres=fn<void(*)(drmModeRes*)>("drmModeFreeResources");
            auto getcon=fn<drmModeConnector*(*)(int,uint32_t)>("drmModeGetConnector");
            auto freecon=fn<void(*)(drmModeConnector*)>("drmModeFreeConnector");
            auto getenc=fn<drmModeEncoder*(*)(int,uint32_t)>("drmModeGetEncoder");
            auto freeenc=fn<void(*)(drmModeEncoder*)>("drmModeFreeEncoder");
            auto getcrtc=fn<drmModeCrtc*(*)(int,uint32_t)>("drmModeGetCrtc");
            auto freecrtc=fn<void(*)(drmModeCrtc*)>("drmModeFreeCrtc");
            unsigned selected = screen && !strcmp(screen,"bottom") ? 1 : 2;
            drmModeRes *r=getres(fd);
            if(!r) throw std::runtime_error("No DRM resources");
            for(int i=0;i<r->count_connectors;i++) {
                drmModeConnector *c=getcon(fd,r->connectors[i]);
                if(c && c->connector_type==DRM_MODE_CONNECTOR_DSI && c->connector_type_id==selected
                    && c->connection==DRM_MODE_CONNECTED && c->count_modes) {
                    connector=c->connector_id; mode=c->modes[0];
                    for(int j=0;j<c->count_modes;j++) if(c->modes[j].type&DRM_MODE_TYPE_PREFERRED) { mode=c->modes[j]; break; }
                    uint32_t eid=c->encoder_id?c->encoder_id:(c->count_encoders?c->encoders[0]:0);
                    drmModeEncoder *e=eid?getenc(fd,eid):nullptr;
                    if(e) {
                        crtc=e->crtc_id;
                        for(int j=0;!crtc && j<r->count_crtcs;j++) if(e->possible_crtcs&(1u<<j)) crtc=r->crtcs[j];
                        freeenc(e);
                    }
                }
                if(c) freecon(c);
                if(connector && crtc) break;
            }
            freeres(r);
            if(!connector || !crtc || mode.hdisplay!=1024 || mode.vdisplay!=768)
                throw std::runtime_error("Selected DS panel/mode unavailable");
            drmModeCrtc *old=getcrtc(fd,crtc);
            if(!old) throw std::runtime_error("Cannot save CRTC");
            saved=*old; freecrtc(old);
            fprintf(stderr,"EASYCAST_DRM screen=%s connector=%u crtc=%u %ux%u master acquired before Vulkan\n",selected==2?"top":"bottom",connector,crtc,mode.hdisplay,mode.vdisplay);
        } catch(...) { closeDrm(); throw; }
    }
    ~EasycastDrmPresenter() { clear(); closeDrm(); }
    void closeDrm() noexcept {
        if(fd>=0) { auto drop=reinterpret_cast<int(*)(int)>(dlsym(lib,"drmDropMaster")); if(drop)drop(fd); close(fd); fd=-1; }
        if(lib) { dlclose(lib); lib=nullptr; }
    }
    void clear() noexcept {
        if(modeset) {
            try { waitFlip(); } catch(const std::exception&e) { fprintf(stderr,"EASYCAST_DRM cleanup %s\n",e.what()); }
            auto set=reinterpret_cast<int(*)(int,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t*,int,drmModeModeInfo*)>(dlsym(lib,"drmModeSetCrtc"));
            int r=(saved.mode_valid&&saved.buffer_id)?set(fd,crtc,saved.buffer_id,saved.x,saved.y,&connector,1,&saved.mode):set(fd,crtc,0,0,0,nullptr,0,nullptr);
            fprintf(stderr,"EASYCAST_DRM restore=%d errno=%d\n",r,r?errno:0);
            modeset=false; pending=false;
        }
        auto &vk=VULKAN_HPP_DEFAULT_DISPATCHER;
        for(auto &b:buffers) {
            if(b.fb) reinterpret_cast<int(*)(int,uint32_t)>(dlsym(lib,"drmModeRmFB"))(fd,b.fb);
            if(b.image) vk.vkDestroyImage(dev,b.image,nullptr);
            if(b.memory) vk.vkFreeMemory(dev,b.memory,nullptr);
            if(b.handle) { drm_mode_destroy_dumb d{}; d.handle=b.handle; ioctl(fd,DRM_IOCTL_MODE_DESTROY_DUMB,&d); }
        }
        buffers.clear(); dev=VK_NULL_HANDLE;
    }
    std::vector<vk::Image> create(vk::Device device, unsigned count) {
        clear(); dev=static_cast<VkDevice>(device); buffers.resize(count);
        std::vector<vk::Image> images; auto &vk=VULKAN_HPP_DEFAULT_DISPATCHER;
        auto prime=fn<int(*)(int,uint32_t,uint32_t,int*)>("drmPrimeHandleToFD");
        auto addfb=fn<int(*)(int,uint32_t,uint32_t,uint32_t,const uint32_t*,const uint32_t*,const uint32_t*,const uint64_t*,uint32_t*,uint32_t)>("drmModeAddFB2WithModifiers");
        for(auto &b:buffers) {
            drm_mode_create_dumb bo{}; bo.width=mode.hdisplay; bo.height=mode.vdisplay; bo.bpp=32;
            if(ioctl(fd,DRM_IOCTL_MODE_CREATE_DUMB,&bo)) throw std::runtime_error("DRM dumb allocation failed");
            b.handle=bo.handle;
            VkSubresourceLayout layout{}; layout.rowPitch=bo.pitch;
            VkImageDrmFormatModifierExplicitCreateInfoEXT modifier{};
            modifier.sType=VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
            modifier.drmFormatModifier=0; modifier.drmFormatModifierPlaneCount=1; modifier.pPlaneLayouts=&layout;
            VkExternalMemoryImageCreateInfo ext{}; ext.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
            ext.pNext=&modifier; ext.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            VkImageCreateInfo ci{}; ci.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; ci.pNext=&ext;
            ci.imageType=VK_IMAGE_TYPE_2D; ci.format=VK_FORMAT_R8G8B8A8_UNORM;
            ci.extent={mode.hdisplay,mode.vdisplay,1}; ci.mipLevels=1; ci.arrayLayers=1;
            ci.samples=VK_SAMPLE_COUNT_1_BIT; ci.tiling=VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
            ci.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
            check(vk.vkCreateImage(dev,&ci,nullptr,&b.image),"Create DRM image");
            VkMemoryRequirements req{}; vk.vkGetImageMemoryRequirements(dev,b.image,&req);
            int dma=-1; if(prime(fd,b.handle,DRM_CLOEXEC|DRM_RDWR,&dma)) throw std::runtime_error("DRM PRIME export failed");
            VkMemoryFdPropertiesKHR props{}; props.sType=VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
            VkResult result=vk.vkGetMemoryFdPropertiesKHR(dev,VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,dma,&props);
            if(result!=VK_SUCCESS) { close(dma); check(result,"Get DMA memory properties"); }
            uint32_t bits=req.memoryTypeBits&props.memoryTypeBits, type=0;
            if(!bits || req.size>bo.size) { close(dma); throw std::runtime_error("DRM/Vulkan memory layout incompatible"); }
            while(!(bits&(1u<<type))) ++type;
            VkMemoryDedicatedAllocateInfo dedicated{}; dedicated.sType=VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO; dedicated.image=b.image;
            VkImportMemoryFdInfoKHR import{}; import.sType=VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR; import.pNext=&dedicated;
            import.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT; import.fd=dma;
            VkMemoryAllocateInfo ai{}; ai.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.pNext=&import;
            ai.allocationSize=req.size; ai.memoryTypeIndex=type;
            result=vk.vkAllocateMemory(dev,&ai,nullptr,&b.memory);
            if(result!=VK_SUCCESS) { close(dma); check(result,"Import DRM memory"); }
            check(vk.vkBindImageMemory(dev,b.image,b.memory,0),"Bind DRM memory");
            uint32_t handles[4]={b.handle},pitches[4]={bo.pitch},offsets[4]={0}; uint64_t mods[4]={0};
            if(addfb(fd,mode.hdisplay,mode.vdisplay,DRM_FORMAT_ABGR8888,handles,pitches,offsets,mods,&b.fb,DRM_MODE_FB_MODIFIERS))
                throw std::runtime_error("DRM framebuffer creation failed");
            images.emplace_back(b.image);
        }
        fprintf(stderr,"EASYCAST_DRM imported %u buffers, no CPU frame copy\n",count);
        return images;
    }
    void barrier(vk::CommandBuffer cb, unsigned index, unsigned queue, bool acquire) {
        auto &b=buffers.at(index);
        VkImageMemoryBarrier bi{}; bi.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bi.image=b.image; bi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        bi.oldLayout=acquire?(b.used?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED):VK_IMAGE_LAYOUT_GENERAL;
        bi.newLayout=acquire?VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_GENERAL;
        const uint32_t outside=foreignQueue?VK_QUEUE_FAMILY_FOREIGN_EXT:VK_QUEUE_FAMILY_EXTERNAL;
        bi.srcQueueFamilyIndex=acquire?outside:queue;
        bi.dstQueueFamilyIndex=acquire?queue:outside;
        bi.srcAccessMask=acquire?0:VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        bi.dstAccessMask=acquire?(VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT):0;
        VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdPipelineBarrier(static_cast<VkCommandBuffer>(cb),acquire?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            acquire?VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&bi);
        b.used=true;
    }
    void present(unsigned index) {
        waitFlip();
        if(!modeset) {
            auto set=fn<int(*)(int,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t*,int,drmModeModeInfo*)>("drmModeSetCrtc");
            if(set(fd,crtc,buffers.at(index).fb,0,0,&connector,1,&mode)) throw std::runtime_error("Initial DRM modeset failed");
            modeset=true;
        } else {
            auto flip=fn<int(*)(int,uint32_t,uint32_t,uint32_t,void*)>("drmModePageFlip");
            if(flip(fd,crtc,buffers.at(index).fb,DRM_MODE_PAGE_FLIP_EVENT,&pending)) throw std::runtime_error("DRM page flip failed");
            pending=true;
        }
    }
    void waitFlipPublic() { waitFlip(); }
    bool isModeset() const { return modeset; }
    unsigned width() const { return mode.hdisplay; }
    unsigned height() const { return mode.vdisplay; }
};
static std::unique_ptr<EasycastDrmPresenter> easycastDrm;
#include "async_present.h"
