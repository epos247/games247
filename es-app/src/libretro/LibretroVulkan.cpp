//  SPDX-License-Identifier: MIT
//
//  games247
//  LibretroVulkan.cpp
//
//  Vulkan for the built-in player's GPU cores, see LibretroVulkan.h.
//

#include "libretro/LibretroVulkan.h"

#include "Log.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstring>

namespace
{
    // The Vulkan loader, opened once and kept for the rest of the run.
    void* sLoader {nullptr};
    bool sLoaderTried {false};
    PFN_vkGetInstanceProcAddr sGetInstanceProcAddr {nullptr};

    PFN_vkGetInstanceProcAddr openLoader()
    {
        if (sLoaderTried)
            return sGetInstanceProcAddr;
        sLoaderTried = true;
        static const char* const names[] {
#if defined(_WIN64)
            "vulkan-1.dll",
#elif defined(__APPLE__)
            "libvulkan.1.dylib",
            "libvulkan.dylib",
            "/usr/local/lib/libvulkan.1.dylib",
            "/opt/homebrew/lib/libvulkan.1.dylib",
            "libMoltenVK.dylib",
            "/usr/local/lib/libMoltenVK.dylib",
            "/opt/homebrew/lib/libMoltenVK.dylib",
#else
            "libvulkan.so.1",
            "libvulkan.so",
#endif
        };
        for (const char* name : names) {
            sLoader = SDL_LoadObject(name);
            if (sLoader == nullptr)
                continue;
            sGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                SDL_LoadFunction(sLoader, "vkGetInstanceProcAddr"));
            if (sGetInstanceProcAddr != nullptr) {
                LOG(LogInfo) << "LibretroVulkan: using the Vulkan loader " << name;
                return sGetInstanceProcAddr;
            }
            SDL_UnloadObject(sLoader);
            sLoader = nullptr;
        }
        LOG(LogInfo) << "LibretroVulkan: no Vulkan loader found";
        return nullptr;
    }

    bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
    {
        return std::any_of(extensions.cbegin(), extensions.cend(),
                           [name](const VkExtensionProperties& extension) {
                               return std::strcmp(extension.extensionName, name) == 0;
                           });
    }

    bool listed(const std::vector<const char*>& names, const char* name)
    {
        return std::any_of(names.cbegin(), names.cend(), [name](const char* listedName) {
            return std::strcmp(listedName, name) == 0;
        });
    }

    // Formats whose pixels are converted to BGRA8888 on the CPU. Others are blitted to a
    // BGRA8888 image on the GPU first.
    enum class PixelLayout { NONE, BGRA8, RGBA8, A2B10G10R10, A2R10G10B10 };

    PixelLayout pixelLayout(VkFormat format)
    {
        switch (format) {
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_SRGB:
                return PixelLayout::BGRA8;
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
            case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
                return PixelLayout::RGBA8;
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
                return PixelLayout::A2B10G10R10;
            case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
                return PixelLayout::A2R10G10B10;
            default:
                return PixelLayout::NONE;
        }
    }

    bool isSRGB(VkFormat format)
    {
        switch (format) {
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_B8G8R8A8_SRGB:
            case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
            case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
                return true;
            default:
                return false;
        }
    }
} // namespace

LibretroVulkan::LibretroVulkan()
    : mNegotiation {nullptr}
    , mInterface {}
    , mDefaultAppInfo {}
    , mInstance {VK_NULL_HANDLE}
    , mGPU {VK_NULL_HANDLE}
    , mDevice {VK_NULL_HANDLE}
    , mQueue {VK_NULL_HANDLE}
    , mQueueFamily {0}
    , mCoreCreatedDevice {false}
    , mPortabilitySubset {false}
    , mHasImage {false}
    , mImage {}
    , mImageQueueFamily {VK_QUEUE_FAMILY_IGNORED}
    , mSignalSemaphore {VK_NULL_HANDLE}
    , mCommandPool {VK_NULL_HANDLE}
    , mCommandBuffer {VK_NULL_HANDLE}
    , mFence {VK_NULL_HANDLE}
    , mBuffer {VK_NULL_HANDLE}
    , mBufferMemory {VK_NULL_HANDLE}
    , mBufferSize {0}
    , mBufferCoherent {false}
    , mBufferMapped {nullptr}
    , mBlitImage {VK_NULL_HANDLE}
    , mBlitMemory {VK_NULL_HANDLE}
    , mBlitWidth {0}
    , mBlitHeight {0}
    , mBlitFormat {VK_FORMAT_UNDEFINED}
    , mLoggedFormat {false}
    , vkGetInstanceProcAddr_ {nullptr}
    , vkCreateInstance_ {nullptr}
    , vkEnumerateInstanceExtensionProperties_ {nullptr}
    , vkEnumerateInstanceVersion_ {nullptr}
    , vkDestroyInstance_ {nullptr}
    , vkEnumeratePhysicalDevices_ {nullptr}
    , vkGetPhysicalDeviceProperties_ {nullptr}
    , vkGetPhysicalDeviceQueueFamilyProperties_ {nullptr}
    , vkGetPhysicalDeviceMemoryProperties_ {nullptr}
    , vkGetPhysicalDeviceFormatProperties_ {nullptr}
    , vkEnumerateDeviceExtensionProperties_ {nullptr}
    , vkCreateDevice_ {nullptr}
    , vkGetDeviceProcAddr_ {nullptr}
    , vkDestroyDevice_ {nullptr}
    , vkGetDeviceQueue_ {nullptr}
    , vkDeviceWaitIdle_ {nullptr}
    , vkCreateCommandPool_ {nullptr}
    , vkDestroyCommandPool_ {nullptr}
    , vkAllocateCommandBuffers_ {nullptr}
    , vkResetCommandBuffer_ {nullptr}
    , vkBeginCommandBuffer_ {nullptr}
    , vkEndCommandBuffer_ {nullptr}
    , vkCmdPipelineBarrier_ {nullptr}
    , vkCmdCopyImageToBuffer_ {nullptr}
    , vkCmdBlitImage_ {nullptr}
    , vkCreateFence_ {nullptr}
    , vkDestroyFence_ {nullptr}
    , vkWaitForFences_ {nullptr}
    , vkResetFences_ {nullptr}
    , vkQueueSubmit_ {nullptr}
    , vkCreateBuffer_ {nullptr}
    , vkDestroyBuffer_ {nullptr}
    , vkGetBufferMemoryRequirements_ {nullptr}
    , vkCreateImage_ {nullptr}
    , vkDestroyImage_ {nullptr}
    , vkGetImageMemoryRequirements_ {nullptr}
    , vkAllocateMemory_ {nullptr}
    , vkFreeMemory_ {nullptr}
    , vkBindBufferMemory_ {nullptr}
    , vkBindImageMemory_ {nullptr}
    , vkMapMemory_ {nullptr}
    , vkUnmapMemory_ {nullptr}
    , vkInvalidateMappedMemoryRanges_ {nullptr}
{
    mDefaultAppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    mDefaultAppInfo.pApplicationName = "games247";
    mDefaultAppInfo.pEngineName = "games247";
    mDefaultAppInfo.apiVersion = VK_API_VERSION_1_1;
}

LibretroVulkan::~LibretroVulkan() { destroy(); }

bool LibretroVulkan::loaderAvailable() { return openLoader() != nullptr; }

// ---------------------------------------------------------------------------------------------
// Creating the instance and device.

bool LibretroVulkan::create(std::string& error)
{
    destroy();
    vkGetInstanceProcAddr_ = openLoader();
    if (vkGetInstanceProcAddr_ == nullptr) {
        error = "Vulkan isn't available on this computer";
        return false;
    }
    if (mNegotiation != nullptr &&
        mNegotiation->interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
        mNegotiation = nullptr;

    if (!createInstance(error) || !loadInstanceFunctions()) {
        if (error.empty())
            error = "couldn't set up Vulkan";
        destroy();
        return false;
    }
    const VkPhysicalDevice gpu {pickGPU()};
    if (gpu == VK_NULL_HANDLE) {
        error = "no graphics card with Vulkan support was found";
        destroy();
        return false;
    }
    if (!createDevice(gpu, error) || !loadDeviceFunctions() || !createCopyResources(error)) {
        if (error.empty())
            error = "couldn't set up the Vulkan device";
        destroy();
        return false;
    }

    VkPhysicalDeviceProperties properties {};
    vkGetPhysicalDeviceProperties_(mGPU, &properties);
    LOG(LogInfo) << "LibretroVulkan: using " << properties.deviceName << " (Vulkan "
                 << VK_API_VERSION_MAJOR(properties.apiVersion) << "."
                 << VK_API_VERSION_MINOR(properties.apiVersion) << "), device created by "
                 << (mCoreCreatedDevice ? "the core" : "games247");

    mInterface = {};
    mInterface.interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN;
    mInterface.interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION;
    mInterface.handle = this;
    mInterface.instance = mInstance;
    mInterface.gpu = mGPU;
    mInterface.device = mDevice;
    mInterface.get_device_proc_addr = vkGetDeviceProcAddr_;
    mInterface.get_instance_proc_addr = vkGetInstanceProcAddr_;
    mInterface.queue = mQueue;
    mInterface.queue_index = mQueueFamily;
    mInterface.set_image = &LibretroVulkan::setImage;
    mInterface.get_sync_index = &LibretroVulkan::getSyncIndex;
    mInterface.get_sync_index_mask = &LibretroVulkan::getSyncIndexMask;
    mInterface.set_command_buffers = &LibretroVulkan::setCommandBuffers;
    mInterface.wait_sync_index = &LibretroVulkan::waitSyncIndex;
    mInterface.lock_queue = &LibretroVulkan::lockQueue;
    mInterface.unlock_queue = &LibretroVulkan::unlockQueue;
    mInterface.set_signal_semaphore = &LibretroVulkan::setSignalSemaphore;
    return true;
}

bool LibretroVulkan::createInstance(std::string& error)
{
    vkCreateInstance_ = reinterpret_cast<PFN_vkCreateInstance>(
        vkGetInstanceProcAddr_(VK_NULL_HANDLE, "vkCreateInstance"));
    vkEnumerateInstanceExtensionProperties_ =
        reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
            vkGetInstanceProcAddr_(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
    vkEnumerateInstanceVersion_ = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        vkGetInstanceProcAddr_(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (vkCreateInstance_ == nullptr || vkEnumerateInstanceExtensionProperties_ == nullptr) {
        error = "the Vulkan loader is incomplete";
        return false;
    }

    const VkApplicationInfo* appInfo {nullptr};
    if (mNegotiation != nullptr && mNegotiation->get_application_info != nullptr)
        appInfo = mNegotiation->get_application_info();
    if (appInfo == nullptr)
        appInfo = &mDefaultAppInfo;

    if (mNegotiation != nullptr && mNegotiation->interface_version >= 2 &&
        mNegotiation->create_instance != nullptr) {
        mInstance = mNegotiation->create_instance(
            vkGetInstanceProcAddr_, appInfo, &LibretroVulkan::createInstanceWrapper, this);
    }
    else {
        VkInstanceCreateInfo info {};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = appInfo;
        mInstance = createInstanceWrapper(this, &info);
    }
    if (mInstance == VK_NULL_HANDLE) {
        error = "couldn't create a Vulkan instance";
        return false;
    }
    return true;
}

VkInstance LibretroVulkan::createInstanceWrapper(void* opaque, const VkInstanceCreateInfo* info)
{
    LibretroVulkan& vulkan {*static_cast<LibretroVulkan*>(opaque)};

    uint32_t count {0};
    vulkan.vkEnumerateInstanceExtensionProperties_(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vulkan.vkEnumerateInstanceExtensionProperties_(nullptr, &count, available.data());

    VkInstanceCreateInfo ourInfo {*info};
    std::vector<const char*> extensions(info->ppEnabledExtensionNames,
                                        info->ppEnabledExtensionNames +
                                            info->enabledExtensionCount);
    // MoltenVK (macOS) is only listed with portability enumeration.
    if (hasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        if (!listed(extensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
            extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        ourInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    if (hasExtension(available, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) &&
        !listed(extensions, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
        extensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    ourInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ourInfo.ppEnabledExtensionNames = extensions.data();

    // Don't ask for a newer Vulkan than the loader has.
    VkApplicationInfo appInfo {};
    if (info->pApplicationInfo != nullptr) {
        appInfo = *info->pApplicationInfo;
        uint32_t loaderVersion {VK_API_VERSION_1_0};
        if (vulkan.vkEnumerateInstanceVersion_ != nullptr)
            vulkan.vkEnumerateInstanceVersion_(&loaderVersion);
        if (appInfo.apiVersion > loaderVersion)
            appInfo.apiVersion = loaderVersion;
        ourInfo.pApplicationInfo = &appInfo;
    }

    VkInstance instance {VK_NULL_HANDLE};
    const VkResult result {vulkan.vkCreateInstance_(&ourInfo, nullptr, &instance)};
    if (result != VK_SUCCESS) {
        LOG(LogError) << "LibretroVulkan: vkCreateInstance failed (" << result << ")";
        return VK_NULL_HANDLE;
    }
    return instance;
}

bool LibretroVulkan::loadInstanceFunctions()
{
#define LOAD_INSTANCE(name)                                                                       \
    name##_ = reinterpret_cast<PFN_##name>(vkGetInstanceProcAddr_(mInstance, #name));             \
    if (name##_ == nullptr) {                                                                     \
        LOG(LogError) << "LibretroVulkan: missing " #name;                                        \
        return false;                                                                             \
    }
    LOAD_INSTANCE(vkDestroyInstance)
    LOAD_INSTANCE(vkEnumeratePhysicalDevices)
    LOAD_INSTANCE(vkGetPhysicalDeviceProperties)
    LOAD_INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties)
    LOAD_INSTANCE(vkGetPhysicalDeviceMemoryProperties)
    LOAD_INSTANCE(vkGetPhysicalDeviceFormatProperties)
    LOAD_INSTANCE(vkEnumerateDeviceExtensionProperties)
    LOAD_INSTANCE(vkCreateDevice)
    LOAD_INSTANCE(vkGetDeviceProcAddr)
#undef LOAD_INSTANCE
    return true;
}

VkPhysicalDevice LibretroVulkan::pickGPU() const
{
    uint32_t count {0};
    vkEnumeratePhysicalDevices_(mInstance, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    vkEnumeratePhysicalDevices_(mInstance, &count, gpus.data());

    // A separate graphics card first, then one built into the processor, then anything else.
    VkPhysicalDevice best {VK_NULL_HANDLE};
    int bestScore {-1};
    for (VkPhysicalDevice gpu : gpus) {
        VkPhysicalDeviceProperties properties {};
        vkGetPhysicalDeviceProperties_(gpu, &properties);
        const int score {properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU   ? 3 :
                         properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 :
                         properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU            ? 0 :
                                                                                           1};
        if (score > bestScore) {
            best = gpu;
            bestScore = score;
        }
    }
    return best;
}

bool LibretroVulkan::createDevice(VkPhysicalDevice gpu, std::string& error)
{
    uint32_t count {0};
    vkEnumerateDeviceExtensionProperties_(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties_(gpu, nullptr, &count, available.data());
    mPortabilitySubset = hasExtension(available, "VK_KHR_portability_subset");

    retro_vulkan_context context {};
    bool created {false};
    if (mNegotiation != nullptr && mNegotiation->interface_version >= 2 &&
        mNegotiation->create_device2 != nullptr) {
        created = mNegotiation->create_device2(&context, mInstance, gpu, VK_NULL_HANDLE,
                                               vkGetInstanceProcAddr_,
                                               &LibretroVulkan::createDeviceWrapper, this);
        // The core may not like that graphics card; let it pick one itself.
        if (!created) {
            context = {};
            created = mNegotiation->create_device2(&context, mInstance, VK_NULL_HANDLE,
                                                   VK_NULL_HANDLE, vkGetInstanceProcAddr_,
                                                   &LibretroVulkan::createDeviceWrapper, this);
        }
    }
    else if (mNegotiation != nullptr && mNegotiation->create_device != nullptr) {
        std::vector<const char*> extensions;
        if (mPortabilitySubset)
            extensions.emplace_back("VK_KHR_portability_subset");
        VkPhysicalDeviceFeatures features {};
        created = mNegotiation->create_device(
            &context, mInstance, gpu, VK_NULL_HANDLE, vkGetInstanceProcAddr_,
            extensions.empty() ? nullptr : extensions.data(),
            static_cast<unsigned int>(extensions.size()), nullptr, 0, &features);
    }

    if (created) {
        mGPU = context.gpu != VK_NULL_HANDLE ? context.gpu : gpu;
        mDevice = context.device;
        mQueue = context.queue;
        mQueueFamily = context.queue_family_index;
        mCoreCreatedDevice = true;
        return mDevice != VK_NULL_HANDLE && mQueue != VK_NULL_HANDLE;
    }
    return createDefaultDevice(gpu, error);
}

bool LibretroVulkan::createDefaultDevice(VkPhysicalDevice gpu, std::string& error)
{
    uint32_t count {0};
    vkGetPhysicalDeviceQueueFamilyProperties_(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties_(gpu, &count, families.data());

    uint32_t family {UINT32_MAX};
    for (uint32_t i {0}; i < count && family == UINT32_MAX; ++i) {
        const VkQueueFlags wanted {VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT};
        if ((families[i].queueFlags & wanted) == wanted)
            family = i;
    }
    if (family == UINT32_MAX) {
        error = "the graphics card has no Vulkan graphics queue";
        return false;
    }

    const float priority {1.0f};
    VkDeviceQueueCreateInfo queueInfo {};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    mDevice = createDeviceWrapper(gpu, this, &info);
    if (mDevice == VK_NULL_HANDLE) {
        error = "couldn't create a Vulkan device";
        return false;
    }
    mGPU = gpu;
    mQueueFamily = family;
    PFN_vkGetDeviceQueue getDeviceQueue {
        reinterpret_cast<PFN_vkGetDeviceQueue>(vkGetDeviceProcAddr_(mDevice, "vkGetDeviceQueue"))};
    getDeviceQueue(mDevice, family, 0, &mQueue);
    return mQueue != VK_NULL_HANDLE;
}

VkDevice LibretroVulkan::createDeviceWrapper(VkPhysicalDevice gpu,
                                             void* opaque,
                                             const VkDeviceCreateInfo* info)
{
    LibretroVulkan& vulkan {*static_cast<LibretroVulkan*>(opaque)};

    uint32_t count {0};
    vulkan.vkEnumerateDeviceExtensionProperties_(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vulkan.vkEnumerateDeviceExtensionProperties_(gpu, nullptr, &count, available.data());

    VkDeviceCreateInfo ourInfo {*info};
    std::vector<const char*> extensions(info->ppEnabledExtensionNames,
                                        info->ppEnabledExtensionNames +
                                            info->enabledExtensionCount);
    // Required on devices that have it (MoltenVK).
    if (hasExtension(available, "VK_KHR_portability_subset") &&
        !listed(extensions, "VK_KHR_portability_subset"))
        extensions.emplace_back("VK_KHR_portability_subset");
    ourInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ourInfo.ppEnabledExtensionNames = extensions.data();

    VkDevice device {VK_NULL_HANDLE};
    const VkResult result {vulkan.vkCreateDevice_(gpu, &ourInfo, nullptr, &device)};
    if (result != VK_SUCCESS) {
        LOG(LogError) << "LibretroVulkan: vkCreateDevice failed (" << result << ")";
        return VK_NULL_HANDLE;
    }
    return device;
}

bool LibretroVulkan::loadDeviceFunctions()
{
#define LOAD_DEVICE(name)                                                                         \
    name##_ = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr_(mDevice, #name));                 \
    if (name##_ == nullptr) {                                                                     \
        LOG(LogError) << "LibretroVulkan: missing " #name;                                        \
        return false;                                                                             \
    }
    LOAD_DEVICE(vkDestroyDevice)
    LOAD_DEVICE(vkGetDeviceQueue)
    LOAD_DEVICE(vkDeviceWaitIdle)
    LOAD_DEVICE(vkCreateCommandPool)
    LOAD_DEVICE(vkDestroyCommandPool)
    LOAD_DEVICE(vkAllocateCommandBuffers)
    LOAD_DEVICE(vkResetCommandBuffer)
    LOAD_DEVICE(vkGetFenceStatus)
    LOAD_DEVICE(vkBeginCommandBuffer)
    LOAD_DEVICE(vkEndCommandBuffer)
    LOAD_DEVICE(vkCmdPipelineBarrier)
    LOAD_DEVICE(vkCmdCopyImageToBuffer)
    LOAD_DEVICE(vkCmdBlitImage)
    LOAD_DEVICE(vkCreateFence)
    LOAD_DEVICE(vkDestroyFence)
    LOAD_DEVICE(vkWaitForFences)
    LOAD_DEVICE(vkResetFences)
    LOAD_DEVICE(vkQueueSubmit)
    LOAD_DEVICE(vkCreateBuffer)
    LOAD_DEVICE(vkDestroyBuffer)
    LOAD_DEVICE(vkGetBufferMemoryRequirements)
    LOAD_DEVICE(vkCreateImage)
    LOAD_DEVICE(vkDestroyImage)
    LOAD_DEVICE(vkGetImageMemoryRequirements)
    LOAD_DEVICE(vkAllocateMemory)
    LOAD_DEVICE(vkFreeMemory)
    LOAD_DEVICE(vkBindBufferMemory)
    LOAD_DEVICE(vkBindImageMemory)
    LOAD_DEVICE(vkMapMemory)
    LOAD_DEVICE(vkUnmapMemory)
    LOAD_DEVICE(vkInvalidateMappedMemoryRanges)
#undef LOAD_DEVICE
    return true;
}

bool LibretroVulkan::createCopyResources(std::string& error)
{
    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = mQueueFamily;
    VkCommandBufferAllocateInfo allocateInfo {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    VkFenceCreateInfo fenceInfo {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    if (vkCreateCommandPool_(mDevice, &poolInfo, nullptr, &mCommandPool) != VK_SUCCESS) {
        error = "couldn't create a Vulkan command pool";
        return false;
    }
    allocateInfo.commandPool = mCommandPool;
    if (vkAllocateCommandBuffers_(mDevice, &allocateInfo, &mCommandBuffer) != VK_SUCCESS ||
        vkCreateFence_(mDevice, &fenceInfo, nullptr, &mFence) != VK_SUCCESS) {
        error = "couldn't create Vulkan command buffers";
        return false;
    }
    return true;
}

void LibretroVulkan::destroy()
{
    if (mDevice != VK_NULL_HANDLE) {
        vkDeviceWaitIdle_(mDevice);
        destroyBlitImage();
        if (mBufferMapped != nullptr)
            vkUnmapMemory_(mDevice, mBufferMemory);
        if (mBuffer != VK_NULL_HANDLE)
            vkDestroyBuffer_(mDevice, mBuffer, nullptr);
        if (mBufferMemory != VK_NULL_HANDLE)
            vkFreeMemory_(mDevice, mBufferMemory, nullptr);
        if (mFence != VK_NULL_HANDLE)
            vkDestroyFence_(mDevice, mFence, nullptr);
        if (mCommandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool_(mDevice, mCommandPool, nullptr);
    }
    // The core frees what it made itself for the device before the device goes.
    if (mCoreCreatedDevice && mNegotiation != nullptr && mNegotiation->destroy_device != nullptr)
        mNegotiation->destroy_device();
    if (mDevice != VK_NULL_HANDLE && vkDestroyDevice_ != nullptr)
        vkDestroyDevice_(mDevice, nullptr);
    if (mInstance != VK_NULL_HANDLE && vkDestroyInstance_ != nullptr)
        vkDestroyInstance_(mInstance, nullptr);

    mBufferMapped = nullptr;
    mBuffer = VK_NULL_HANDLE;
    mBufferMemory = VK_NULL_HANDLE;
    mBufferSize = 0;
    mFence = VK_NULL_HANDLE;
    mCommandPool = VK_NULL_HANDLE;
    mCommandBuffer = VK_NULL_HANDLE;
    mDevice = VK_NULL_HANDLE;
    mInstance = VK_NULL_HANDLE;
    mGPU = VK_NULL_HANDLE;
    mQueue = VK_NULL_HANDLE;
    mCoreCreatedDevice = false;
    mHasImage = false;
    mFramePending = false;
    mCopyInFlight = false;
    mLoggedStuck = false;
    mWaitSemaphores.clear();
    mCoreCommandBuffers.clear();
    mSignalSemaphore = VK_NULL_HANDLE;
    mInterface = {};
}

// ---------------------------------------------------------------------------------------------
// The interface the core calls.

void LibretroVulkan::setImage(void* handle,
                              const retro_vulkan_image* image,
                              uint32_t numSemaphores,
                              const VkSemaphore* semaphores,
                              uint32_t srcQueueFamily)
{
    LibretroVulkan& vulkan {*static_cast<LibretroVulkan*>(handle)};
    if (image == nullptr) {
        vulkan.mHasImage = false;
        return;
    }
    // Only the image view's create info is kept; its pNext can't be relied on afterwards.
    vulkan.mImage = *image;
    vulkan.mImage.create_info.pNext = nullptr;
    vulkan.mImageQueueFamily = srcQueueFamily;
    vulkan.mHasImage = true;
    vulkan.mWaitSemaphores.assign(semaphores, semaphores + numSemaphores);
}

// One frame in flight: every frame is finished on the GPU before retro_run returns.
uint32_t LibretroVulkan::getSyncIndex(void*) { return 0; }
uint32_t LibretroVulkan::getSyncIndexMask(void*) { return 1; }
void LibretroVulkan::waitSyncIndex(void*) {}

void LibretroVulkan::setCommandBuffers(void* handle, uint32_t num, const VkCommandBuffer* cmd)
{
    LibretroVulkan& vulkan {*static_cast<LibretroVulkan*>(handle)};
    vulkan.mCoreCommandBuffers.assign(cmd, cmd + num);
}

void LibretroVulkan::lockQueue(void* handle)
{
    static_cast<LibretroVulkan*>(handle)->mQueueMutex.lock();
}

void LibretroVulkan::unlockQueue(void* handle)
{
    static_cast<LibretroVulkan*>(handle)->mQueueMutex.unlock();
}

void LibretroVulkan::setSignalSemaphore(void* handle, VkSemaphore semaphore)
{
    static_cast<LibretroVulkan*>(handle)->mSignalSemaphore = semaphore;
}

// ---------------------------------------------------------------------------------------------
// Copying frames out.

int LibretroVulkan::findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags wanted) const
{
    VkPhysicalDeviceMemoryProperties properties {};
    vkGetPhysicalDeviceMemoryProperties_(mGPU, &properties);
    for (uint32_t i {0}; i < properties.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) != 0 &&
            (properties.memoryTypes[i].propertyFlags & wanted) == wanted)
            return static_cast<int>(i);
    }
    return -1;
}

bool LibretroVulkan::ensureBuffer(VkDeviceSize size)
{
    if (mBuffer != VK_NULL_HANDLE && mBufferSize >= size)
        return true;
    if (mBufferMapped != nullptr)
        vkUnmapMemory_(mDevice, mBufferMemory);
    if (mBuffer != VK_NULL_HANDLE)
        vkDestroyBuffer_(mDevice, mBuffer, nullptr);
    if (mBufferMemory != VK_NULL_HANDLE)
        vkFreeMemory_(mDevice, mBufferMemory, nullptr);
    mBufferMapped = nullptr;
    mBuffer = VK_NULL_HANDLE;
    mBufferMemory = VK_NULL_HANDLE;
    mBufferSize = 0;

    VkBufferCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer_(mDevice, &info, nullptr, &mBuffer) != VK_SUCCESS)
        return false;

    VkMemoryRequirements requirements {};
    vkGetBufferMemoryRequirements_(mDevice, mBuffer, &requirements);
    // Cached memory reads back much faster; coherent memory needs no invalidating.
    int type {findMemoryType(requirements.memoryTypeBits,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_CACHED_BIT)};
    if (type < 0)
        type = findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (type < 0)
        return false;
    VkPhysicalDeviceMemoryProperties properties {};
    vkGetPhysicalDeviceMemoryProperties_(mGPU, &properties);
    mBufferCoherent = (properties.memoryTypes[type].propertyFlags &
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

    VkMemoryAllocateInfo allocateInfo {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = static_cast<uint32_t>(type);
    if (vkAllocateMemory_(mDevice, &allocateInfo, nullptr, &mBufferMemory) != VK_SUCCESS ||
        vkBindBufferMemory_(mDevice, mBuffer, mBufferMemory, 0) != VK_SUCCESS ||
        vkMapMemory_(mDevice, mBufferMemory, 0, VK_WHOLE_SIZE, 0, &mBufferMapped) != VK_SUCCESS) {
        mBufferMapped = nullptr;
        return false;
    }
    mBufferSize = size;
    return true;
}

bool LibretroVulkan::ensureBlitImage(unsigned int width, unsigned int height, VkFormat format)
{
    if (mBlitImage != VK_NULL_HANDLE && mBlitWidth == width && mBlitHeight == height &&
        mBlitFormat == format)
        return true;
    destroyBlitImage();

    VkImageCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage_(mDevice, &info, nullptr, &mBlitImage) != VK_SUCCESS) {
        mBlitImage = VK_NULL_HANDLE;
        return false;
    }
    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements_(mDevice, mBlitImage, &requirements);
    const int type {
        findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
    VkMemoryAllocateInfo allocateInfo {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = static_cast<uint32_t>(std::max(type, 0));
    if (type < 0 ||
        vkAllocateMemory_(mDevice, &allocateInfo, nullptr, &mBlitMemory) != VK_SUCCESS ||
        vkBindImageMemory_(mDevice, mBlitImage, mBlitMemory, 0) != VK_SUCCESS) {
        destroyBlitImage();
        return false;
    }
    mBlitWidth = width;
    mBlitHeight = height;
    mBlitFormat = format;
    return true;
}

void LibretroVulkan::destroyBlitImage()
{
    if (mBlitImage != VK_NULL_HANDLE)
        vkDestroyImage_(mDevice, mBlitImage, nullptr);
    if (mBlitMemory != VK_NULL_HANDLE)
        vkFreeMemory_(mDevice, mBlitMemory, nullptr);
    mBlitImage = VK_NULL_HANDLE;
    mBlitMemory = VK_NULL_HANDLE;
    mBlitWidth = 0;
    mBlitHeight = 0;
    mBlitFormat = VK_FORMAT_UNDEFINED;
}

bool LibretroVulkan::submit(VkCommandBuffer ours, VkFence fence)
{
    std::vector<VkCommandBuffer> commandBuffers {mCoreCommandBuffers};
    if (ours != VK_NULL_HANDLE)
        commandBuffers.emplace_back(ours);
    // With the core's own command buffers, its semaphores aren't waited for (it uses
    // barriers instead).
    std::vector<VkSemaphore> waits;
    if (mCoreCommandBuffers.empty())
        waits = mWaitSemaphores;
    if (commandBuffers.empty() && waits.empty() && mSignalSemaphore == VK_NULL_HANDLE &&
        fence == VK_NULL_HANDLE)
        return true;
    const std::vector<VkPipelineStageFlags> waitStages(waits.size(),
                                                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);

    VkSubmitInfo info {};
    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    info.waitSemaphoreCount = static_cast<uint32_t>(waits.size());
    info.pWaitSemaphores = waits.data();
    info.pWaitDstStageMask = waitStages.data();
    info.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
    info.pCommandBuffers = commandBuffers.data();
    info.signalSemaphoreCount = mSignalSemaphore != VK_NULL_HANDLE ? 1 : 0;
    info.pSignalSemaphores = &mSignalSemaphore;

    // Each is used once.
    mWaitSemaphores.clear();
    mCoreCommandBuffers.clear();
    mSignalSemaphore = VK_NULL_HANDLE;

    if (fence != VK_NULL_HANDLE)
        vkResetFences_(mDevice, 1, &fence);
    VkResult result;
    {
        std::lock_guard<std::recursive_mutex> lock {mQueueMutex};
        result = vkQueueSubmit_(mQueue, 1, &info, fence);
    }
    if (result != VK_SUCCESS) {
        LOG(LogError) << "LibretroVulkan: vkQueueSubmit failed (" << result << ")";
        return false;
    }
    return true;
}

void LibretroVulkan::dupeFrame()
{
    if (mDevice == VK_NULL_HANDLE)
        return;
    if (mSignalSemaphore != VK_NULL_HANDLE || !mCoreCommandBuffers.empty()) {
        mWaitSemaphores.clear();
        submit(VK_NULL_HANDLE, VK_NULL_HANDLE);
    }
}

void LibretroVulkan::frameReady(unsigned int width, unsigned int height)
{
    mPendingWidth = width;
    mPendingHeight = height;
    mFramePending = true;
}

bool LibretroVulkan::collectFrame(std::vector<uint8_t>& bgra,
                                  unsigned int& width,
                                  unsigned int& height)
{
    if (mDevice == VK_NULL_HANDLE)
        return false;
    bool newPicture {false};
    const auto finishCopy = [&]() {
        convertCopy(bgra);
        width = mCopyWidth;
        height = mCopyHeight;
        mCopyInFlight = false;
        mLoggedStuck = false;
        newPicture = true;
    };

    if (mCopyInFlight) {
        const VkResult status {vkGetFenceStatus_(mDevice, mFence)};
        if (status == VK_SUCCESS) {
            finishCopy();
        }
        else if (!mLoggedStuck &&
                 (status != VK_NOT_READY ||
                  std::chrono::steady_clock::now() - mCopyStart > std::chrono::seconds(5))) {
            LOG(LogError) << "LibretroVulkan: the core's picture still isn't ready after 5 "
                             "seconds (fence status "
                          << status << ")";
            Log::flush();
            mLoggedStuck = true;
        }
    }

    if (mFramePending) {
        mFramePending = false;
        if (mCopyInFlight) {
            // The last copy isn't done: skip this picture, but still pass the core's
            // semaphores and command buffers on, as each must be used once.
            submit(VK_NULL_HANDLE, VK_NULL_HANDLE);
        }
        else if (startCopy(mPendingWidth, mPendingHeight)) {
            // Usually done within a millisecond or two; otherwise it's picked up next time.
            if (vkWaitForFences_(mDevice, 1, &mFence, VK_TRUE, 50ull * 1000 * 1000) ==
                VK_SUCCESS)
                finishCopy();
        }
    }
    return newPicture;
}

bool LibretroVulkan::startCopy(unsigned int width, unsigned int height)
{
    if (mDevice == VK_NULL_HANDLE || !mHasImage || width == 0 || height == 0)
        return false;

    const VkImage image {mImage.create_info.image};
    const VkFormat format {mImage.create_info.format};
    const VkImageLayout layout {mImage.image_layout};
    PixelLayout pixels {pixelLayout(format)};
    VkFormat blitFormat {VK_FORMAT_UNDEFINED};
    if (pixels == PixelLayout::NONE) {
        VkFormatProperties properties {};
        vkGetPhysicalDeviceFormatProperties_(mGPU, format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) == 0) {
            if (!mLoggedFormat) {
                LOG(LogError) << "LibretroVulkan: can't read pictures in Vulkan format "
                              << format;
            }
            mLoggedFormat = true;
            return false;
        }
        blitFormat = isSRGB(format) ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_B8G8R8A8_UNORM;
        if (!ensureBlitImage(width, height, blitFormat))
            return false;
        pixels = PixelLayout::BGRA8;
    }
    if (!mLoggedFormat) {
        LOG(LogInfo) << "LibretroVulkan: core pictures are Vulkan format " << format
                     << (blitFormat != VK_FORMAT_UNDEFINED ? ", converted on the GPU" : "");
        mLoggedFormat = true;
    }
    if (!ensureBuffer(static_cast<VkDeviceSize>(width) * height * 4))
        return false;

    VkImageSubresourceRange range {mImage.create_info.subresourceRange};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    const VkImageSubresourceLayers layers {VK_IMAGE_ASPECT_COLOR_BIT, range.baseMipLevel,
                                           range.baseArrayLayer, 1};
    // An image in GENERAL layout mustn't be transitioned; it can be read as it is.
    const VkImageLayout readLayout {layout == VK_IMAGE_LAYOUT_GENERAL ?
                                        VK_IMAGE_LAYOUT_GENERAL :
                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    const bool transferOwnership {mImageQueueFamily != VK_QUEUE_FAMILY_IGNORED &&
                                  mImageQueueFamily != mQueueFamily};

    vkResetCommandBuffer_(mCommandBuffer, 0);
    VkCommandBufferBeginInfo begin {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer_(mCommandBuffer, &begin);

    VkImageMemoryBarrier toRead {};
    toRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toRead.srcAccessMask = transferOwnership ? 0 : VK_ACCESS_MEMORY_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toRead.oldLayout = layout;
    toRead.newLayout = readLayout;
    toRead.srcQueueFamilyIndex = transferOwnership ? mImageQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    toRead.dstQueueFamilyIndex = transferOwnership ? mQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    toRead.image = image;
    toRead.subresourceRange = range;
    vkCmdPipelineBarrier_(mCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);

    VkBufferImageCopy copy {};
    copy.imageExtent = {width, height, 1};
    if (blitFormat != VK_FORMAT_UNDEFINED) {
        VkImageMemoryBarrier blitBarrier {};
        blitBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        blitBarrier.srcAccessMask = 0;
        blitBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        blitBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        blitBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        blitBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        blitBarrier.image = mBlitImage;
        blitBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier_(mCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                              &blitBarrier);
        VkImageBlit blit {};
        blit.srcSubresource = layers;
        blit.srcOffsets[1] = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.dstOffsets[1] = blit.srcOffsets[1];
        vkCmdBlitImage_(mCommandBuffer, image, readLayout, mBlitImage,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
        blitBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        blitBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        blitBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blitBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        vkCmdPipelineBarrier_(mCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                              &blitBarrier);
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        vkCmdCopyImageToBuffer_(mCommandBuffer, mBlitImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                mBuffer, 1, &copy);
    }
    else {
        copy.imageSubresource = layers;
        vkCmdCopyImageToBuffer_(mCommandBuffer, image, readLayout, mBuffer, 1, &copy);
    }

    // Give the image back to the core as it was.
    VkImageMemoryBarrier toCore {toRead};
    toCore.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toCore.dstAccessMask = 0;
    toCore.oldLayout = readLayout;
    toCore.newLayout = layout;
    toCore.srcQueueFamilyIndex = transferOwnership ? mQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    toCore.dstQueueFamilyIndex = transferOwnership ? mImageQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    VkBufferMemoryBarrier toHost {};
    toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.buffer = mBuffer;
    toHost.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier_(mCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                          nullptr, 1, &toHost, 1, &toCore);
    vkEndCommandBuffer_(mCommandBuffer);

    if (!submit(mCommandBuffer, mFence))
        return false;
    mCopyInFlight = true;
    mCopyWidth = width;
    mCopyHeight = height;
    mCopyPixels = static_cast<int>(pixels);
    mCopyStart = std::chrono::steady_clock::now();
    return true;
}

void LibretroVulkan::convertCopy(std::vector<uint8_t>& bgra) const
{
    const unsigned int width {mCopyWidth};
    const unsigned int height {mCopyHeight};
    const PixelLayout pixels {static_cast<PixelLayout>(mCopyPixels)};
    if (!mBufferCoherent) {
        VkMappedMemoryRange memoryRange {};
        memoryRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        memoryRange.memory = mBufferMemory;
        memoryRange.size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges_(mDevice, 1, &memoryRange);
    }

    // To BGRA8888, opaque.
    const size_t count {static_cast<size_t>(width) * height};
    bgra.resize(count * 4);
    const uint8_t* in {static_cast<const uint8_t*>(mBufferMapped)};
    uint8_t* out {bgra.data()};
    switch (pixels) {
        case PixelLayout::BGRA8:
            for (size_t i {0}; i < count; ++i, in += 4, out += 4) {
                out[0] = in[0];
                out[1] = in[1];
                out[2] = in[2];
                out[3] = 0xFF;
            }
            break;
        case PixelLayout::RGBA8:
            for (size_t i {0}; i < count; ++i, in += 4, out += 4) {
                out[0] = in[2];
                out[1] = in[1];
                out[2] = in[0];
                out[3] = 0xFF;
            }
            break;
        case PixelLayout::A2B10G10R10:
        case PixelLayout::A2R10G10B10: {
            const bool redLow {pixels == PixelLayout::A2B10G10R10};
            for (size_t i {0}; i < count; ++i, in += 4, out += 4) {
                uint32_t value;
                std::memcpy(&value, in, 4);
                const uint8_t low {static_cast<uint8_t>((value >> 2) & 0xFF)};
                const uint8_t green {static_cast<uint8_t>((value >> 12) & 0xFF)};
                const uint8_t high {static_cast<uint8_t>((value >> 22) & 0xFF)};
                out[0] = redLow ? high : low;
                out[1] = green;
                out[2] = redLow ? low : high;
                out[3] = 0xFF;
            }
            break;
        }
        case PixelLayout::NONE:
            break;
    }
}
