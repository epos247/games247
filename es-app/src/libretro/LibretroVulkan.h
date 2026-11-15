//  SPDX-License-Identifier: MIT
//
//  games247
//  LibretroVulkan.h
//
//  Vulkan for the built-in player's GPU cores (for example Xenia Edge for the Xbox 360). The
//  core draws with Vulkan on a device created here, or by the core itself through its context
//  negotiation interface, and hands each finished picture over with set_image(). That picture
//  is copied into memory and shown like a software core's frame, the same way OpenGL cores'
//  frames are. Nothing is presented with Vulkan, so no window surface is needed.
//
//  The Vulkan loader is opened at run time (vulkan-1.dll on Windows), so games247 still starts
//  on a computer without Vulkan; only Vulkan cores then fall back to RetroArch.
//

#ifndef GAMES247_LIBRETRO_VULKAN_H
#define GAMES247_LIBRETRO_VULKAN_H

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "libretro/libretro_vulkan.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class LibretroVulkan
{
public:
    LibretroVulkan();
    ~LibretroVulkan();

    // True if the Vulkan loader could be opened.
    static bool loaderAvailable();

    // The core's context negotiation interface (SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE),
    // or none. Call before create().
    void setNegotiation(const retro_hw_render_context_negotiation_interface_vulkan* negotiation)
    {
        mNegotiation = negotiation;
    }

    // Creates the instance, device and queue. On failure, returns false and fills error.
    bool create(std::string& error);
    void destroy();
    bool isCreated() const { return mDevice != VK_NULL_HANDLE; }

    // What GET_HW_RENDER_INTERFACE gives the core.
    const retro_hw_render_interface_vulkan* getInterface() const { return &mInterface; }

    // After the core passes RETRO_HW_FRAME_BUFFER_VALID: copies the top-left width x height of
    // the image from set_image() into bgra (BGRA8888, top row first). Returns false if there's
    // no picture to copy.
    bool readFrame(unsigned int width, unsigned int height, std::vector<uint8_t>& bgra);
    // For a repeated frame (a null picture): still signals the core's signal semaphore.
    void dupeFrame();

private:
    // The interface functions given to the core; handle is the LibretroVulkan.
    static void setImage(void* handle,
                         const retro_vulkan_image* image,
                         uint32_t numSemaphores,
                         const VkSemaphore* semaphores,
                         uint32_t srcQueueFamily);
    static uint32_t getSyncIndex(void* handle);
    static uint32_t getSyncIndexMask(void* handle);
    static void setCommandBuffers(void* handle, uint32_t num, const VkCommandBuffer* cmd);
    static void waitSyncIndex(void* handle);
    static void lockQueue(void* handle);
    static void unlockQueue(void* handle);
    static void setSignalSemaphore(void* handle, VkSemaphore semaphore);
    // The wrappers the core calls instead of vkCreateInstance and vkCreateDevice, so the
    // extensions games247 needs are added.
    static VkInstance createInstanceWrapper(void* opaque, const VkInstanceCreateInfo* info);
    static VkDevice createDeviceWrapper(VkPhysicalDevice gpu,
                                        void* opaque,
                                        const VkDeviceCreateInfo* info);

    bool loadInstanceFunctions();
    bool loadDeviceFunctions();
    bool createInstance(std::string& error);
    VkPhysicalDevice pickGPU() const;
    bool createDevice(VkPhysicalDevice gpu, std::string& error);
    bool createDefaultDevice(VkPhysicalDevice gpu, std::string& error);
    bool createCopyResources(std::string& error);
    bool ensureBuffer(VkDeviceSize size);
    bool ensureBlitImage(unsigned int width, unsigned int height, VkFormat format);
    void destroyBlitImage();
    int findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags wanted) const;
    // Submits the core's command buffers, our own and the semaphores, and waits for the GPU.
    bool submitAndWait(VkCommandBuffer ours);

    const retro_hw_render_context_negotiation_interface_vulkan* mNegotiation;
    retro_hw_render_interface_vulkan mInterface;
    VkApplicationInfo mDefaultAppInfo;

    VkInstance mInstance;
    VkPhysicalDevice mGPU;
    VkDevice mDevice;
    VkQueue mQueue;
    uint32_t mQueueFamily;
    bool mCoreCreatedDevice;
    bool mPortabilitySubset;

    // The picture from set_image(), until it's read.
    bool mHasImage;
    retro_vulkan_image mImage;
    uint32_t mImageQueueFamily;
    std::vector<VkSemaphore> mWaitSemaphores;
    std::vector<VkCommandBuffer> mCoreCommandBuffers;
    VkSemaphore mSignalSemaphore;
    std::recursive_mutex mQueueMutex;

    // Copying the picture out.
    VkCommandPool mCommandPool;
    VkCommandBuffer mCommandBuffer;
    VkFence mFence;
    VkBuffer mBuffer;
    VkDeviceMemory mBufferMemory;
    VkDeviceSize mBufferSize;
    bool mBufferCoherent;
    void* mBufferMapped;
    // For formats that can't be converted on the CPU: the picture is blitted to this
    // BGRA8888 image first.
    VkImage mBlitImage;
    VkDeviceMemory mBlitMemory;
    unsigned int mBlitWidth;
    unsigned int mBlitHeight;
    VkFormat mBlitFormat;
    bool mLoggedFormat;

    // Vulkan functions.
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_;
    PFN_vkCreateInstance vkCreateInstance_;
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties_;
    PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion_;
    PFN_vkDestroyInstance vkDestroyInstance_;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices_;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties_;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties_;
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties_;
    PFN_vkGetPhysicalDeviceFormatProperties vkGetPhysicalDeviceFormatProperties_;
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties_;
    PFN_vkCreateDevice vkCreateDevice_;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr_;
    PFN_vkDestroyDevice vkDestroyDevice_;
    PFN_vkGetDeviceQueue vkGetDeviceQueue_;
    PFN_vkDeviceWaitIdle vkDeviceWaitIdle_;
    PFN_vkCreateCommandPool vkCreateCommandPool_;
    PFN_vkDestroyCommandPool vkDestroyCommandPool_;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers_;
    PFN_vkResetCommandBuffer vkResetCommandBuffer_;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer_;
    PFN_vkEndCommandBuffer vkEndCommandBuffer_;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier_;
    PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer_;
    PFN_vkCmdBlitImage vkCmdBlitImage_;
    PFN_vkCreateFence vkCreateFence_;
    PFN_vkDestroyFence vkDestroyFence_;
    PFN_vkWaitForFences vkWaitForFences_;
    PFN_vkResetFences vkResetFences_;
    PFN_vkQueueSubmit vkQueueSubmit_;
    PFN_vkCreateBuffer vkCreateBuffer_;
    PFN_vkDestroyBuffer vkDestroyBuffer_;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements_;
    PFN_vkCreateImage vkCreateImage_;
    PFN_vkDestroyImage vkDestroyImage_;
    PFN_vkGetImageMemoryRequirements vkGetImageMemoryRequirements_;
    PFN_vkAllocateMemory vkAllocateMemory_;
    PFN_vkFreeMemory vkFreeMemory_;
    PFN_vkBindBufferMemory vkBindBufferMemory_;
    PFN_vkBindImageMemory vkBindImageMemory_;
    PFN_vkMapMemory vkMapMemory_;
    PFN_vkUnmapMemory vkUnmapMemory_;
    PFN_vkInvalidateMappedMemoryRanges vkInvalidateMappedMemoryRanges_;
};

#endif // GAMES247_LIBRETRO_VULKAN_H
