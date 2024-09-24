#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_beta.h>

#include <cjson/cJSON.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define DEFAULT_WINDOW_WIDTH 800
#define DEFAULT_WINDOW_HEIGHT 600
#define PROJECT_NAME "Vulkan Engine"

#define ENABLE_VALIDATION_LAYERS true
#define ALLOW_DEVICE_WITHOUT_INTEGRATED_GPU true
#define ALLOW_DEVICE_WITHOUT_GEOMETRY_SHADER true

#define UINT32_UNITIALIZED_VALUE UINT32_MAX
#define UINT32_INVALIDED_VALUE 0xDEADBEEF

#define REQUIRED_VULKAN_API_VERSION VK_API_VERSION_1_3

#define REQUIRED_DEVICE_EXTENSIONS {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME}

#define MAX_FRAMES_IN_FLIGHT 2

// PANIC macro to print error details (with file, line, and function info) and abort
// While not very clean I am explicitly fine with memory leaks when PANIC is called during the initialization
// as the program gets terminated, might clean that up later on, maybe build some custom unique_ptr setup for the initialziation.
#define PANIC(fmt, ...) \
    do { \
        fprintf(stderr, "PANIC in function %s (file: %s, line: %d): ", __func__, __FILE__, __LINE__); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
        fprintf(stderr, "\n"); \
        abort(); \
    } while(0)

// This is a workaround to the fact that
// const char* err_msg = "asd";
// PANIC(err_msg)
// does not work, we need ot call PANIC("%s", err_msg) instead which is exactly what this new macro does.
#define PANIC_STR(msg) PANIC("%s", msg)

// If we are in debug mode (i.e. when NDEBUG is false), we use this malloc/realloc with metadata
// to track if malloc ever returns a NULL handle.
// I might later expand this to track the memory with an id to look for memory leaks.
#ifndef NDEBUG
    #define malloc(size) debug_malloc(size)
    #define realloc(ptr, size) debug_realloc(ptr, size)

    void* debug_malloc(const size_t size) {
        printf("malloc(size=%zu)\n", size);
        #undef malloc
        void* ptr = malloc(size);  // Call the real malloc
        #define malloc(size) debug_malloc(size)
        if(ptr == NULL) PANIC("Failed to allocate %zu bytes.", size);
        return ptr;
    }

    void* debug_realloc(void* ptr, const size_t size) {
    #undef realloc
            void* new_ptr = realloc(ptr, size);  // Call the real realloc
    #define realloc(ptr, size) debug_realloc(ptr, size)
            if(new_ptr == NULL) PANIC("Failed to reallocate %zu bytes.", size);
            return new_ptr;
        }
#endif // not NDEBUG


SDL_Window* g_window;

VkInstance g_instance = VK_NULL_HANDLE;
VkSurfaceKHR g_surface = VK_NULL_HANDLE;
VkPhysicalDevice g_physical_device = VK_NULL_HANDLE;
VkDevice g_device = VK_NULL_HANDLE;
VkSwapchainKHR g_swap_chain = VK_NULL_HANDLE;
VkImage* g_swap_chain_images = NULL;
uint32_t g_num_swap_chain_images = 0;
VkFormat g_swap_chain_image_format = VK_NULL_HANDLE;
VkImageView* g_swap_chain_image_views = VK_NULL_HANDLE;
VkExtent2D g_swap_chain_extent;

VkQueue g_graphics_queue = VK_NULL_HANDLE;
VkQueue g_presentation_queue = VK_NULL_HANDLE;

VkDebugUtilsMessengerEXT g_debug_messenger;

bool g_is_running = true;

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData
) {
    (void)messageSeverity; (void)messageType; (void)pUserData; // Suppressed "Unused Parameter" warning
    fprintf(stderr, "validation layer: %s\n", pCallbackData->pMessage);
    return VK_FALSE;
}

void initWindow() {
    printf("Trying to initialize window.\n");

    if(SDL_Init(SDL_INIT_VIDEO) != 0) {
        PANIC_STR(SDL_GetError());
    }

    if(!SDL_Vulkan_LoadLibrary(NULL)) {
        printf("Vulkan support is available.\n");
    } else {
        const char* err_msg = SDL_GetError();
        SDL_Quit();
        PANIC_STR(err_msg);
    }
    SDL_Vulkan_LoadLibrary(NULL);

    g_window = SDL_CreateWindow(
        PROJECT_NAME,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        DEFAULT_WINDOW_WIDTH,
        DEFAULT_WINDOW_HEIGHT,
        SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN
    );

    if(!g_window) {
        const char* error_message = SDL_GetError(); SDL_Quit();
        PANIC_STR(error_message);
    }

    SDL_SetWindowResizable(g_window, SDL_FALSE);

    printf("Successfully initialized window.\n");
}

void handleInput(const SDL_Event e) {
    if(e.type == SDL_QUIT) {
        printf("Got a SLD_QUIT event!\n");
        g_is_running = false;
    }
    if(e.type == SDL_KEYDOWN) {
        if(e.key.keysym.sym == SDLK_ESCAPE) {
            printf("Escape key pressed, exiting...\n"),
            g_is_running = false;
        }
    }
}

//@DS:NEEDS_FREE_AFTER_USE
const char** getRequiredExtensions(uint32_t* extensionCount) {
    unsigned int sdlExtensionCount = 0;

    if(!SDL_Vulkan_GetInstanceExtensions(NULL, &sdlExtensionCount, NULL)) {
        SDL_Log("Could not get Vulkan instance extensions: %s", SDL_GetError());
        return NULL;
    }

    const char** sdlExtensions = malloc(sdlExtensionCount * sizeof(const char*));

    // Get the actual extension names
    if(!SDL_Vulkan_GetInstanceExtensions(NULL, &sdlExtensionCount, sdlExtensions)) {
        SDL_Log("Could not get Vulkan instance extensions: %s", SDL_GetError());
        free(sdlExtensions);
        return NULL;
    }

    *extensionCount = sdlExtensionCount;
    const char** extensions = malloc(sdlExtensionCount * sizeof(const char*));
    memcpy(extensions, sdlExtensions, sdlExtensionCount * sizeof(const char*));
    free(sdlExtensions);

    if(ENABLE_VALIDATION_LAYERS) {
        const char** temp = realloc(extensions, (*extensionCount + 1) * sizeof(const char*));
        extensions = temp;
        extensions[*extensionCount] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        *extensionCount += 1;
    }

    return extensions;
}

void checkValidationLayerSupport() {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);
    if(layer_count == 0) PANIC("No Instance Layers supported, so in particular no validation layers!");

    printf("Checking Validation Layer Support.\n");
    VkLayerProperties* available_layers = malloc(layer_count * sizeof(VkLayerProperties));
    vkEnumerateInstanceLayerProperties(&layer_count, available_layers);
    bool found = false;
    for(size_t i = 0; i < layer_count; i++) {
        if(strcmp(available_layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            found = true;
            break;
        }
    }
    free(available_layers); available_layers = NULL;

    if(!found) PANIC("Validation layer is not supported.");
    printf("Validation layer is supported.\n");
}

void initInstance() {
    if(ENABLE_VALIDATION_LAYERS) { checkValidationLayerSupport(); }

    uint32_t apiVersion = 0;
    vkEnumerateInstanceVersion(&apiVersion);
    if(apiVersion < REQUIRED_VULKAN_API_VERSION) {
        PANIC("Available Vulkan API version %u.%u.%u < %u.%u.%u!",
            VK_API_VERSION_MAJOR(apiVersion), VK_API_VERSION_MINOR(apiVersion), VK_API_VERSION_PATCH(apiVersion),
            VK_API_VERSION_MAJOR(REQUIRED_VULKAN_API_VERSION), VK_API_VERSION_MINOR(REQUIRED_VULKAN_API_VERSION), VK_API_VERSION_PATCH(REQUIRED_VULKAN_API_VERSION)
        );
    }

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "Daniels Vulkan Engine",
        .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(0, 0, 1),
        .apiVersion = REQUIRED_VULKAN_API_VERSION};

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL};

    uint32_t required_extension_count;
    // ReSharper disable once CppDFAMemoryLeak
    const char** required_extensions = getRequiredExtensions(&required_extension_count);

#if defined(__APPLE__) && defined(__arm64__)
    required_extensions = realloc(required_extensions, (required_extension_count + 2) * sizeof(const char*));
    required_extensions[required_extension_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    required_extensions[required_extension_count++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    create_info.enabledExtensionCount = required_extension_count;
    create_info.ppEnabledExtensionNames = required_extensions;

    uint32_t available_extension_count;
    vkEnumerateInstanceExtensionProperties(NULL, &available_extension_count, NULL);
    VkExtensionProperties* available_extensions = malloc(available_extension_count * sizeof(VkExtensionProperties));
    vkEnumerateInstanceExtensionProperties(NULL, &available_extension_count, available_extensions);
    for(int i = 0; i < required_extension_count; i++) {
        bool found = false;
        for(int j = 0; j < available_extension_count; j++) {
            if(strcmp(required_extensions[i], available_extensions[j].extensionName) == 0) {
                found = true;
                break;
            }
        }
        if(!found) PANIC("Failed to find required extension '%s'", required_extensions[i]);
    }


    if(ENABLE_VALIDATION_LAYERS) {
        const VkDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugCallback};
        create_info.ppEnabledLayerNames = (const char*[]){"VK_LAYER_KHRONOS_validation"};
        create_info.enabledLayerCount = 1;
        create_info.pNext = &debug_utils_messenger_create_info;
    } else {
        create_info.enabledLayerCount = 0; create_info.pNext = NULL;
    }

    if(vkCreateInstance(&create_info, NULL, &g_instance) != VK_SUCCESS) PANIC("Failed to create Vulkan instance!");
    free(required_extensions);

    if(ENABLE_VALIDATION_LAYERS) {
        const VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debugCallback};

        const PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(g_instance, "vkCreateDebugUtilsMessengerEXT");
        if(func == NULL || func(g_instance, &debug_messenger_create_info, NULL, &g_debug_messenger) != VK_SUCCESS) {
            PANIC("Failed to initialize debug utils messenger.");
        }
    }
}

typedef struct {
    uint32_t graphicsFamily;
    uint32_t presentationFamily;
} QueueFamilyIndices;

bool QueueFamilyIndices_isComplete(const QueueFamilyIndices* pQFI) {
    return (pQFI->graphicsFamily != UINT32_UNITIALIZED_VALUE) && (pQFI->presentationFamily != UINT32_UNITIALIZED_VALUE);
}


// Returns QFI of a suitable queue, if none is suitable then this panics.
QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices = {
        .graphicsFamily = UINT32_UNITIALIZED_VALUE,
        .presentationFamily = UINT32_UNITIALIZED_VALUE};

    uint32_t num_queue_families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &num_queue_families, NULL);

    VkQueueFamilyProperties* queueFamilies = malloc(num_queue_families * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &num_queue_families, queueFamilies);

    for(int i = 0; i < num_queue_families; i++) {
        if(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, g_surface, &presentSupport);
        if(presentSupport) indices.presentationFamily = i;

        if(QueueFamilyIndices_isComplete(&indices)) break;
    }
    free(queueFamilies);
    return indices;
}

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR* formats;
    uint32_t num_formats;
    VkPresentModeKHR* present_modes;
    uint32_t num_present_modes;
};
void SwapChainSupportDetails_free(struct SwapChainSupportDetails* details) {
    if (details == NULL) return;
    free(details->formats); free(details->present_modes);
    details->formats = NULL; details->present_modes = NULL;

    details->num_formats = UINT32_INVALIDED_VALUE;
    details->num_present_modes = UINT32_INVALIDED_VALUE;
}

//@DS:NEEDS_FREE_AFTER_USE
void querySwapChainSupport(VkPhysicalDevice device, struct SwapChainSupportDetails* details) {
    details->formats=NULL;
    details->num_formats=0;
    details->present_modes=NULL;
    details->num_present_modes=0;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        device,
        g_surface,
        &details->capabilities);

    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device,
        g_surface,
        &details->num_formats,
        details->formats);
    if(details->num_formats != 0) {
        details->formats = malloc(details->num_formats * sizeof(VkSurfaceFormatKHR));
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            device,
            g_surface,
            &details->num_formats,
            details->formats);
    }

    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device,
        g_surface,
        &details->num_present_modes,
        NULL);
    if(details->num_present_modes != 0) {
        details->present_modes = malloc(details->num_present_modes * sizeof(VkPresentModeKHR));
        vkGetPhysicalDeviceSurfacePresentModesKHR(device,
            g_surface,
            &details->num_present_modes,
            details->present_modes);
    }
}

bool isDeviceSuitable(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    QueueFamilyIndices indices = findQueueFamilies(device);
    if(!QueueFamilyIndices_isComplete(&indices)) {
        fprintf(stderr, "Device does not have the necessary queue families.");
        return false;
    }
    printf("Device supports suitable queue families.\n");

    uint32_t num_available_extensions = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &num_available_extensions, NULL);
    VkExtensionProperties* available_extensions = malloc(num_available_extensions * sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(device, NULL, &num_available_extensions, available_extensions);

    const char* required_extensions[] = REQUIRED_DEVICE_EXTENSIONS;
    size_t num_required_extensions = sizeof(required_extensions) / sizeof(required_extensions[0]);
    for(size_t i = 0; i < num_required_extensions; i++) {
        bool found = false;
        for(size_t j = 0; j < num_available_extensions; j++) {
            if(strcmp(available_extensions[j].extensionName, required_extensions[i]) == 0) {
                found = true;
                break;
            }
        }
        if(!found) {
            free(available_extensions);
            return false;
        }
    }
    free(available_extensions);
    printf("Device supports the necessary extensions.\n");

    struct SwapChainSupportDetails details;
    querySwapChainSupport(device, &details);
    bool swapchain_is_supported = (details.num_formats > 0) && (details.num_present_modes > 0);
    SwapChainSupportDetails_free(&details);
    if(!swapchain_is_supported) {
        fprintf(stderr, "Device does not support swapchain.");
        return false;
    }

    VkPhysicalDeviceFeatures supported_features;
    vkGetPhysicalDeviceFeatures(device, &supported_features);
    if(!supported_features.samplerAnisotropy) {
        fprintf(stderr, "Device does not support samplerAnisotropy.");
        return false;
    }
    return true;
}

void pickPhysicalDevice() {
    uint32_t num_physical_devices = 0;
    vkEnumeratePhysicalDevices(g_instance, &num_physical_devices, NULL);
    if(num_physical_devices == 0) PANIC("No physical devices found!");
    VkPhysicalDevice* physical_devices = malloc(num_physical_devices * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(g_instance, &num_physical_devices, physical_devices);

    bool found = false;
    for(size_t i = 0; i < num_physical_devices; i++) {
        VkPhysicalDevice current_device = physical_devices[i];
        if(isDeviceSuitable(current_device)) {
            found = true;
            g_physical_device = current_device;
            break;
        }
    }
    if(!found) PANIC("No suitable physical device available!");
    free(physical_devices);
}

void createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(g_physical_device);

    bool indices_are_same = (indices.presentationFamily == indices.graphicsFamily);
    if(!QueueFamilyIndices_isComplete(&indices)) PANIC("Invalid QueueFamilyIndices");

    VkDeviceQueueCreateInfo* queue_create_infos;
    uint32_t num_queue_create_infos;
    float queuePriority = 1.0f;
    if(indices_are_same) {
        VkDeviceQueueCreateInfo graphics_queue_create_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = indices.graphicsFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority};
        queue_create_infos = malloc(sizeof(VkDeviceQueueCreateInfo));
        queue_create_infos[0] = graphics_queue_create_info;
        num_queue_create_infos = 1;
    } else {
        VkDeviceQueueCreateInfo graphics_queue_create_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = indices.graphicsFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority};
        VkDeviceQueueCreateInfo presentation_queue_create_info = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = indices.graphicsFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority};
        queue_create_infos = malloc(sizeof(VkDeviceQueueCreateInfo));
        queue_create_infos[0] = graphics_queue_create_info;
        queue_create_infos[1] = presentation_queue_create_info;
        num_queue_create_infos = 2;
    }

    VkPhysicalDeviceFeatures device_features = {.samplerAnisotropy = VK_TRUE};

    const char* required_extensions[] = REQUIRED_DEVICE_EXTENSIONS;
    size_t num_required_extensions = sizeof(required_extensions) / sizeof(required_extensions[0]);
    VkDeviceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = num_queue_create_infos,
        .pQueueCreateInfos = queue_create_infos,
        .enabledExtensionCount = num_required_extensions,
        .ppEnabledExtensionNames = required_extensions,
        .pEnabledFeatures = &device_features};
    createInfo.enabledLayerCount = 0;

    if(ENABLE_VALIDATION_LAYERS) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = (const char*[]){"VK_LAYER_KHRONOS_validation"};
    }

    if (vkCreateDevice(g_physical_device, &createInfo, NULL, &g_device) != VK_SUCCESS) PANIC("Failed to create device.");

    vkGetDeviceQueue(g_device, indices.graphicsFamily, 0, &g_graphics_queue);
    vkGetDeviceQueue(g_device, indices.presentationFamily, 0, &g_presentation_queue);
}

VkSurfaceFormatKHR chooseSwapSurfaceFormat(const VkSurfaceFormatKHR* available_formats, const uint32_t num_available_formats) {
    if(num_available_formats == 0) PANIC("No swap surface formats available");

    bool found = false;
    VkSurfaceFormatKHR current_format;
    for(int i = 0; i < num_available_formats; i++) {
        current_format = available_formats[i];
        if((current_format.format == VK_FORMAT_B8G8R8A8_SRGB) && (current_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)) {
            found = true;
            break;
        }
    }
    if(!found) PANIC("No suitable swap format available!");
    // ReSharper disable once CppLocalVariableMightNotBeInitialized
    return current_format;
}

VkPresentModeKHR chooseSwapPresentMode(const VkPresentModeKHR* available_present_modes, const uint32_t num_available_present_modes) {
    if(num_available_present_modes == 0) PANIC("No presentation modes available!");

    bool found = false;
    VkPresentModeKHR current_present_mode;
    for(int i = 0; i < num_available_present_modes; i++) {
        current_present_mode = available_present_modes[i];
        if(current_present_mode == VK_PRESENT_MODE_FIFO_KHR) {
            found = true;
            break;
        }
    }
    if(!found) {
        fprintf(stderr, "Presentation mode VK_PRESENT_MODE_MAILBOX_KHR is not supported, falling back to a random presentation mode.\n");
        return available_present_modes[0];
    }
    return current_present_mode;
}

VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR* capabilities) {
    const bool isExtentUndefined = capabilities->currentExtent.width == UINT32_UNITIALIZED_VALUE;
    if (!isExtentUndefined) return capabilities->currentExtent;

    int width = 0; int height = 0;
    SDL_Vulkan_GetDrawableSize(g_window, &width, &height);
    uint32_t clampedWidth = width;
    if (clampedWidth < capabilities->minImageExtent.width)
        clampedWidth = capabilities->minImageExtent.width;
    if (clampedWidth > capabilities->maxImageExtent.width)
        clampedWidth = capabilities->maxImageExtent.width;

    uint32_t clampedHeight = height;
    if (clampedHeight < capabilities->minImageExtent.height)
        clampedHeight = capabilities->minImageExtent.height;
    if (clampedHeight > capabilities->maxImageExtent.height)
        clampedHeight = capabilities->maxImageExtent.height;
    const VkExtent2D actualExtent = {
        .width = clampedWidth,
        .height = clampedHeight,
    };

    return actualExtent;
}

void createSwapChain() {
    struct SwapChainSupportDetails details;
    querySwapChainSupport(g_physical_device, &details);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(details.formats, details.num_formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(details.present_modes, details.num_present_modes);
    VkExtent2D extent = chooseSwapExtent(&details.capabilities);

    // Limit the number of swap chain images to MAX_FRAMES_IN_FLIGHT
    g_num_swap_chain_images = MAX_FRAMES_IN_FLIGHT;

    // Ensure imageCount is within the allowed range
    if (g_num_swap_chain_images < details.capabilities.minImageCount) g_num_swap_chain_images = details.capabilities.minImageCount;
    if (g_num_swap_chain_images > details.capabilities.maxImageCount) g_num_swap_chain_images = details.capabilities.maxImageCount;
    if(g_num_swap_chain_images == 0) PANIC("swap chain image count is 0!");

    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = g_surface,
        .minImageCount = g_num_swap_chain_images,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .preTransform = details.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    QueueFamilyIndices queue_family_indices = findQueueFamilies(g_physical_device);
    if(!QueueFamilyIndices_isComplete(&queue_family_indices)) PANIC("queueFamilies is not complete!");

    if (queue_family_indices.graphicsFamily != queue_family_indices.presentationFamily) {
        fprintf(stdout, "Setting imageSharingMode to Concurrent.\n");
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = (uint32_t[]){ queue_family_indices.graphicsFamily, queue_family_indices.presentationFamily };
    } else {
        fprintf(stdout, "Setting imageSharingMode to Exclusive.\n");
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = NULL;
    }

    if(vkCreateSwapchainKHR(g_device, &createInfo, NULL, &g_swap_chain) != VK_SUCCESS) PANIC("Failed to create swap chain!");

    vkGetSwapchainImagesKHR(g_device, g_swap_chain, &g_num_swap_chain_images, NULL);
    g_swap_chain_images = malloc(g_num_swap_chain_images * sizeof(VkImage));
    vkGetSwapchainImagesKHR(g_device, g_swap_chain, &g_num_swap_chain_images, g_swap_chain_images);

    g_swap_chain_image_format = surfaceFormat.format;
    g_swap_chain_extent = extent;

    SwapChainSupportDetails_free(&details);
}

VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, const uint32_t mipLevels) {
    const VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspectFlags,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1},
    };
    VkImageView imageView = VK_NULL_HANDLE;
    if (vkCreateImageView(g_device, &viewInfo, NULL, &imageView) != VK_SUCCESS) PANIC("Failed to create texture image view!");
    return imageView;
}

int main() {
    printf("Initializing window.\n");
    initWindow();
    printf("Initializing Instance.\n");
    initInstance();

    printf("Creating Vulkan Surface.\n");
    if(!SDL_Vulkan_CreateSurface(g_window, g_instance, &g_surface)) PANIC("Failed to bind SDL window to VkSurface.");

    printf("Picking Physical Device.\n");
    pickPhysicalDevice();

    printf("Creating Logical Device.\n");
    createLogicalDevice();

    printf("Creating Swap chain.\n");
    createSwapChain();
    g_swap_chain_image_views = malloc(g_num_swap_chain_images * sizeof(VkImageView));

    for (size_t i = 0; i < g_num_swap_chain_images; i++) {
        g_swap_chain_image_views[i] = createImageView(g_swap_chain_images[i], g_swap_chain_image_format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }

    SDL_Event e;
    while (g_is_running){
        while (SDL_PollEvent(&e)){
            handleInput(e);
        }
    }

    for (size_t i = 0; i < g_num_swap_chain_images; i++) {
        vkDestroyImageView(g_device, g_swap_chain_image_views[i], NULL);
    }
    free(g_swap_chain_image_views);
    free(g_swap_chain_images);
    vkDestroySwapchainKHR(g_device, g_swap_chain, NULL);
    vkDestroyDevice(g_device, NULL);
    vkDestroySurfaceKHR(g_instance, g_surface, NULL);
    const PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)(vkGetInstanceProcAddr(g_instance, "vkDestroyDebugUtilsMessengerEXT"));
    if(func != NULL) func(g_instance, g_debug_messenger, NULL);
    g_debug_messenger = NULL;
    vkDestroyInstance(g_instance, NULL);

    if(g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();

    return EXIT_SUCCESS;
}