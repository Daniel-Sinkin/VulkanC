#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_beta.h>
#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arm/limits.h>
#include <sys/stat.h>
#include <cglm/cglm.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tinyobj_loader_c.h>

#define DEFAULT_WINDOW_WIDTH 800
#define DEFAULT_WINDOW_HEIGHT 600
#define PROJECT_NAME "Vulkan Engine"

#define ENABLE_VALIDATION_LAYERS true
#define ALLOW_DEVICE_WITHOUT_INTEGRATED_GPU true
#define ALLOW_DEVICE_WITHOUT_GEOMETRY_SHADER true

#define UINT32_UNINITIALIZED_VALUE UINT32_MAX
#define UINT32_INVALIDED_VALUE 0xDEADBEEF

#define REQUIRED_VULKAN_API_VERSION VK_API_VERSION_1_3

#define REQUIRED_DEVICE_EXTENSIONS {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME}

#define MAX_FRAMES_IN_FLIGHT 2

// Typedef for the Timer struct
typedef struct Timer {
    clock_t start;
    const char *file;
    int line;
    const char *func;
    const char *info;
} Timer;

// Function to start the timer, with file, line, and function info
void start_timer(Timer* t, const char* file, const int line, const char* func) {
    t->start = clock();
    t->file = file;
    t->line = line;
    t->func = func;
}

void stop_timer(const Timer* t) {
    const clock_t end = clock();
    const double elapsed = (double)(end - t->start) / CLOCKS_PER_SEC;
    printf("[[SCOPE_TIMER]] %s | File: %s | Line: %d | Function: %s | Elapsed time: %.3f seconds\n", t->info, t->file, t->line, t->func, elapsed);
}

// Cleanup function: Automatically called when the Timer goes out of scope
void timer_cleanup(const Timer* t) {
    stop_timer(t);
}

// Macro to declare a Timer that automatically stops when leaving scope
#define SCOPE_TIMER __attribute__((cleanup(timer_cleanup))) \
Timer _timer_instance; \
start_timer(&_timer_instance, __FILE__, __LINE__, __func__);


// PANIC macro to print error details (with file, line, and function info) and abort
// While not very clean I am explicitly fine with memory leaks when PANIC is called during the initialization
// as the program gets terminated, might clean that up later on, maybe build some custom unique_ptr setup for the initialization.
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

// If we are in debug mode (i.e., when NDEBUG is false), we use this malloc/realloc with metadata
// to track if malloc ever returns a NULL handle.
#ifndef NDEBUG
    // Redefine malloc and realloc macros to include file, line, and function metadata
    #define malloc(size) debug_malloc(size, __FILE__, __LINE__, __func__)
    #define realloc(ptr, size) debug_realloc(ptr, size, __FILE__, __LINE__, __func__)

    // Debug malloc function with metadata
    void* debug_malloc(const size_t size, const char* file, int line, const char* func) {
        printf("malloc(size=%zu) called from file: %s, line: %d, function: %s\n", size, file, line, func);

        // Temporarily undefine malloc to call the real malloc
    #undef malloc
        void* ptr = malloc(size);  // Call the real malloc
    #define malloc(size) debug_malloc(size, __FILE__, __LINE__, __func__)

        if (ptr == NULL) {
            fprintf(stderr, "PANIC: Failed to allocate %zu bytes in file %s, line %d, function %s\n", size, file, line, func);
            exit(EXIT_FAILURE);  // Replace PANIC with exit for simplicity
        }
        return ptr;
    }

    // Debug realloc function with metadata
    void* debug_realloc(void* ptr, const size_t size, const char* file, int line, const char* func) {
        printf("realloc(ptr=%p, size=%zu) called from file: %s, line: %d, function: %s\n", ptr, size, file, line, func);

        // Temporarily undefine realloc to call the real realloc
    #undef realloc
        void* new_ptr = realloc(ptr, size);  // Call the real realloc
    #define realloc(ptr, size) debug_realloc(ptr, size, __FILE__, __LINE__, __func__)

        if (new_ptr == NULL) {
            fprintf(stderr, "PANIC: Failed to reallocate %zu bytes in file %s, line %d, function %s\n", size, file, line, func);
            exit(EXIT_FAILURE);  // Replace PANIC with exit for simplicity
        }
        return new_ptr;
    }
#endif // not NDEBUG

// Function to check if the file is a regular file using stat
int is_regular_file(const char *filename) {
    struct stat fileStat;
    if (stat(filename, &fileStat) != 0) return 0; // File does not exist or is in an error state
    return S_ISREG(fileStat.st_mode); // Check if it's a regular file
}

//@DS:NEEDS_FREE_AFTER_USE
char *readFile(const char *filename, size_t *out_size) {
    // First check if the file exists and is a regular file
    if (!is_regular_file(filename)) {
        fprintf(stderr, "Error: '%s' is not a regular file or does not exist.\n", filename);
        return NULL;
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Unable to open file '%s': %s\n", filename, strerror(errno));
        return NULL;
    }

    // Seek to the end to determine the file size
    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: Unable to seek to the end of file '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    const long fileSize = ftell(file);
    if (fileSize == -1L) {
        fprintf(stderr, "Error: Unable to get file size of '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    if (fileSize > LONG_MAX) {
        fprintf(stderr, "Error: File size exceeds maximum supported size.\n");
        fclose(file);
        return NULL;
    }

    *out_size = (size_t)fileSize;

    // Go back to the beginning of the file
    rewind(file);

    // Allocate buffer for the file content
    char *buffer = malloc(*out_size);
    if (!buffer) {
        fprintf(stderr, "Error: Memory allocation failed for file '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    // Read the file into the buffer
    const size_t bytesRead = fread(buffer, 1, *out_size, file);
    if (bytesRead != *out_size) {
        fprintf(stderr, "Error: Unable to read entire file '%s'\n", filename);
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    return buffer;
}

typedef struct {
    vec3 cameraEye    __attribute__((aligned(16)));
    vec3 cameraCenter __attribute__((aligned(16)));
    vec3 cameraUp     __attribute__((aligned(16)));
    float time;
    int stage;
} PushConstants;


SDL_Window* g_window;

VkInstance g_instance = VK_NULL_HANDLE;
VkSurfaceKHR g_surface = VK_NULL_HANDLE;
VkPhysicalDevice g_physical_device = VK_NULL_HANDLE;
VkDevice g_device = VK_NULL_HANDLE;

VkSwapchainKHR g_swap_chain = VK_NULL_HANDLE;
VkImage* g_swap_chain_images = NULL;
uint32_t g_num_swap_chain_images = UINT32_UNINITIALIZED_VALUE;
VkFormat g_swap_chain_image_format = VK_NULL_HANDLE;
VkImageView* g_swap_chain_image_views = VK_NULL_HANDLE;
uint32_t g_num_swap_chain_image_views = UINT32_UNINITIALIZED_VALUE;
VkExtent2D g_swap_chain_extent = {.width = UINT32_UNINITIALIZED_VALUE, .height = UINT32_UNINITIALIZED_VALUE};
VkFramebuffer* g_swap_chain_framebuffers = VK_NULL_HANDLE;
uint32_t g_num_swap_chain_framebuffers = UINT32_UNINITIALIZED_VALUE;

VkImage g_color_image;
VkDeviceMemory g_color_image_memory;
VkImageView g_color_image_view;

VkImage g_depth_image;
VkDeviceMemory g_depth_image_memory;
VkImageView g_depth_image_view;

VkDescriptorSetLayout g_descriptor_set_layout = VK_NULL_HANDLE;
VkRenderPass g_render_pass = VK_NULL_HANDLE;

VkPipeline g_graphics_pipeline = VK_NULL_HANDLE;
VkPipelineLayout g_pipeline_layout = VK_NULL_HANDLE;

VkCommandPool g_command_pool = VK_NULL_HANDLE;

VkSampleCountFlagBits g_MSAASamples;

VkQueue g_graphics_queue = VK_NULL_HANDLE;
VkQueue g_presentation_queue = VK_NULL_HANDLE;

VkDebugUtilsMessengerEXT g_debug_messenger;

bool g_is_running = false;

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void *pUserData
) {
    (void)messageType; (void)pUserData; // Suppressed "Unused Parameter" warning
    if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) printf("Validation Layer [INFO]: %s\n", pCallbackData->pMessage);
    else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) fprintf(stderr, "Validation Layer [ERROR]: %s\n", pCallbackData->pMessage);
    else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) printf("Validation Layer [WARNING]: %s\n", pCallbackData->pMessage);
    else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) printf("Validation Layer [VERBOSE]: %s\n", pCallbackData->pMessage);
    else PANIC("Unknown messageSeverity!");
    return VK_FALSE;
}

void initWindow() {
    SCOPE_TIMER;
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
    return (pQFI->graphicsFamily != UINT32_UNINITIALIZED_VALUE) && (pQFI->presentationFamily != UINT32_UNINITIALIZED_VALUE);
}


// Returns QFI of a suitable queue, if none is suitable then this panics.
QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices = {
        .graphicsFamily = UINT32_UNINITIALIZED_VALUE,
        .presentationFamily = UINT32_UNINITIALIZED_VALUE};

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

typedef struct {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR* formats;
    uint32_t num_formats;
    VkPresentModeKHR* present_modes;
    uint32_t num_present_modes;
}SwapChainSupportDetails;

void SwapChainSupportDetails_free(SwapChainSupportDetails* details) {
    if (details == NULL) return;
    free(details->formats); free(details->present_modes);
    details->formats = NULL; details->present_modes = NULL;

    details->num_formats = UINT32_INVALIDED_VALUE;
    details->num_present_modes = UINT32_INVALIDED_VALUE;
}

//@DS:NEEDS_FREE_AFTER_USE
void querySwapChainSupport(VkPhysicalDevice device, SwapChainSupportDetails* details) {
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

    SwapChainSupportDetails details;
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

VkSampleCountFlagBits getMaxUsableSampleCount() {
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(g_physical_device, &physicalDeviceProperties);

    const VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
    if (counts & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
    if (counts & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
    if (counts & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
    if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
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
    g_MSAASamples = getMaxUsableSampleCount();
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
    const bool isExtentUndefined = capabilities->currentExtent.width == UINT32_UNINITIALIZED_VALUE;
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

void createSwapChain() {
    SwapChainSupportDetails details;
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

    g_num_swap_chain_image_views = g_num_swap_chain_images;
    g_swap_chain_image_views = malloc(g_num_swap_chain_image_views * sizeof(VkImageView));

    for (size_t i = 0; i < g_num_swap_chain_image_views; i++) {
        g_swap_chain_image_views[i] = createImageView(g_swap_chain_images[i], g_swap_chain_image_format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
}

bool hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}


VkFormat findSupportedFormat(const VkFormat* candidates, uint32_t num_candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for(size_t i = 0; i < num_candidates; i++) {
        VkFormat format = candidates[i];
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(g_physical_device, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR) {
            if ((props.linearTilingFeatures & features) == features) {
                return format;
            }
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL) {
            if ((props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }
    }
    PANIC("Failed to find supported format!");
}

VkFormat findDepthFormat() {
    return findSupportedFormat(
        (VkFormat[]){VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        3,
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}


void createRenderPass() {
    const VkAttachmentDescription colorAttachment = {
        .format = g_swap_chain_image_format,
        .samples = g_MSAASamples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    const VkAttachmentDescription depthAttachment = {
        .format = findDepthFormat(),
        .samples = g_MSAASamples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    const VkAttachmentDescription colorAttachmentResolve = {
        .format = g_swap_chain_image_format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};

    VkAttachmentReference colorAttachmentRef = {
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkAttachmentReference depthAttachmentRef = {
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkAttachmentReference colorAttachmentResolveRef = {
        .attachment = 2,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription description = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef,
        .pResolveAttachments = &colorAttachmentResolveRef,
        .pDepthStencilAttachment = &depthAttachmentRef};

    VkSubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};

    const VkRenderPassCreateInfo renderPassInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 3,
        .pAttachments = (VkAttachmentDescription[]) { colorAttachment, depthAttachment, colorAttachmentResolve },
        .subpassCount = 1,
        .pSubpasses = &description,
        .dependencyCount = 1,
        .pDependencies = &dependency
    };

    if (vkCreateRenderPass(g_device, &renderPassInfo, NULL, &g_render_pass) != VK_SUCCESS) PANIC("failed to create render pass!");
}

void createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT};

    VkDescriptorSetLayoutBinding samplerLayoutBinding = {
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = NULL};

    const VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = (VkDescriptorSetLayoutBinding[]){uboLayoutBinding, samplerLayoutBinding}};

    if (vkCreateDescriptorSetLayout(g_device, &layoutInfo, NULL, &g_descriptor_set_layout) != VK_SUCCESS) PANIC("failed to create descriptor set layout!");
}

VkShaderModule createShaderModule(const char* code, size_t code_length) {
    const VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = (uint32_t)code_length,
        .pCode = (const uint32_t *)code
    };
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(g_device, &createInfo, NULL, &shaderModule) != VK_SUCCESS) PANIC("Failed to create shader!");
    return shaderModule;
}

typedef struct {
    vec3 pos;
    vec3 normal;
    vec2 texCoord;
} Vertex;

VkVertexInputBindingDescription getVertexBindingDescription() {
    const VkVertexInputBindingDescription bindingDescription = {
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };
    return bindingDescription;
}

VkVertexInputAttributeDescription* getVertexAttributeDescription(uint32_t* num_attribute_descriptions) {
    *num_attribute_descriptions = 3;
    static VkVertexInputAttributeDescription attributes[3];
    attributes[0] = (VkVertexInputAttributeDescription){
        .binding = 0,
        .location = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(Vertex, pos)};
    attributes[1] = (VkVertexInputAttributeDescription){
        .binding = 0,
        .location = 1,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = offsetof(Vertex, normal)};
    attributes[2] = (VkVertexInputAttributeDescription){
        .binding = 0,
        .location = 2,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = offsetof(Vertex, texCoord)};
    return attributes;
}

void createGraphicsPipeline() {
    fprintf(stdout, "Trying to create Shader modules.\n");
    fprintf(stdout, "Trying to read .spv files.\n");
    size_t vertShaderCode_length; size_t fragShaderCode_length;
    // ReSharper disable once CppDFAMemoryLeak
    const char* vertShaderCode = readFile("shaders/compiled/shader_phong_stages.vert.spv", &vertShaderCode_length);
    if (!vertShaderCode) PANIC("Could not read vertex shader file.");
    // ReSharper disable once CppDFAMemoryLeak
    const char* fragShaderCode = readFile("shaders/compiled/shader_phong_stages.frag.spv", &fragShaderCode_length);
    if (!fragShaderCode) PANIC("Could not read fragment shader file.");

    fprintf(stdout, "\tTrying to create Vertex Shader.\n");
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode, vertShaderCode_length);
    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertShaderModule,
        .pName = "main"};

    fprintf(stdout, "\tTrying to create Fragment Shader.\n");
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode, fragShaderCode_length);
    VkPipelineShaderStageCreateInfo fragShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragShaderModule,
        .pName = "main"};

    free((char *)vertShaderCode); vertShaderCode = NULL; free((char *)fragShaderCode); fragShaderCode = NULL;
    vertShaderCode_length = UINT32_INVALIDED_VALUE; fragShaderCode_length = UINT32_INVALIDED_VALUE;
    fprintf(stdout, "Successfully created the shader modules.\n");


    fprintf(stdout, "Trying to Initialize Fixed Functions.\n");
    fprintf(stdout, "\tInitializing Vertex Input.\n");
    VkVertexInputBindingDescription bindingDescription = getVertexBindingDescription();
    uint32_t num_attribute_descriptions;
    VkVertexInputAttributeDescription* attribute_descriptions = getVertexAttributeDescription(&num_attribute_descriptions);

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = num_attribute_descriptions,
        .pVertexAttributeDescriptions = attribute_descriptions};

    fprintf(stdout, "\tInitializing Input Assembly.\n");
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE};

    VkDynamicState dynamicStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates};

    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1};

    fprintf(stdout, "\tInitializing Rasterizer.\n");
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f};

    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE};

    fprintf(stdout, "\tInitializing Multisampling.\n");
    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = g_MSAASamples,
        .sampleShadingEnable = VK_FALSE};

    fprintf(stdout, "\tInitializing Color Blending.\n");
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
        .blendConstants = {0.0F, 0.0F, 0.0F, 0.0F}};

    // Define the push constant range
    VkPushConstantRange pushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants)};

    fprintf(stdout, "\tInitializing Render Pipeline.\n");
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &g_descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange};

    if (vkCreatePipelineLayout(g_device, &pipelineLayoutInfo, NULL, &g_pipeline_layout) != VK_SUCCESS) PANIC("failed to create pipeline layout!");

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = (VkPipelineShaderStageCreateInfo[]) {vertShaderStageInfo, fragShaderStageInfo},
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = g_pipeline_layout,
        .renderPass = g_render_pass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE};

    if (vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &g_graphics_pipeline) != VK_SUCCESS) PANIC("failed to create graphics pipeline!");

    fprintf(stdout, "Cleaning up shader modules.\n");
    vkDestroyShaderModule(g_device, fragShaderModule, NULL);
    vkDestroyShaderModule(g_device, vertShaderModule, NULL);
}

void createCommandPool() {
    QueueFamilyIndices queue_family_indices = findQueueFamilies(g_physical_device);
    if(!QueueFamilyIndices_isComplete(&queue_family_indices)) {
        PANIC("findQueueFamilies returned incomplete indices!");
    }
    const VkCommandPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family_indices.graphicsFamily};

    if (vkCreateCommandPool(g_device, &create_info, NULL, &g_command_pool) != VK_SUCCESS) PANIC("Failed to create command pool!");
}

uint32_t findMemoryType(const uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(g_physical_device, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    PANIC("Couldn't determine the memory type.");
}

void createImage(
    const uint32_t width,
    const uint32_t height,
    const uint32_t mipLevels,
    VkSampleCountFlagBits numSamples,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkImage *image,
    VkDeviceMemory *imageMemory)
{
    const VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = width, .height = height, .depth = 1},
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = numSamples,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    if (vkCreateImage(g_device, &imageInfo, NULL, image) != VK_SUCCESS)
        PANIC("failed to create image!");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(g_device, *image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(g_device, &allocInfo, NULL, imageMemory) != VK_SUCCESS)
        PANIC("failed to allocate image memory!");

    vkBindImageMemory(g_device, *image, *imageMemory, 0);
}


void createColorResources() {
    createImage(
        g_swap_chain_extent.width,
        g_swap_chain_extent.height,
        1,
        g_MSAASamples,
        g_swap_chain_image_format,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &g_color_image,
        &g_color_image_memory);
    g_color_image_view = createImageView(g_color_image, g_swap_chain_image_format, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void createDepthResources() {
    VkFormat depthFormat = findDepthFormat();
    createImage(
        g_swap_chain_extent.width,
        g_swap_chain_extent.height,
        1,
        g_MSAASamples,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &g_depth_image,
        &g_depth_image_memory);
    g_depth_image_view = createImageView(g_depth_image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}

void createFramebuffers() {
    g_num_swap_chain_framebuffers = g_num_swap_chain_image_views;
    g_swap_chain_framebuffers = malloc(g_num_swap_chain_framebuffers * sizeof(VkImageView));
    for (size_t i = 0; i < g_num_swap_chain_framebuffers; i++) {
        fprintf(stdout, "\t%zu. Framebuffers.\n", i + 1);
        VkFramebufferCreateInfo framebufferInfo = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = g_render_pass,
            .attachmentCount = 3,
            .pAttachments = (VkImageView[]){g_color_image_view, g_depth_image_view, g_swap_chain_image_views[i]},
            .width = g_swap_chain_extent.width,
            .height = g_swap_chain_extent.height,
            .layers = 1};
        if (vkCreateFramebuffer(g_device, &framebufferInfo, NULL, &g_swap_chain_framebuffers[i]) != VK_SUCCESS) PANIC("failed to create framebuffer!");
    }
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

    printf("Creating Render Pass.\n");
    createRenderPass();

    printf("Creating descriptor set layout.\n");
    createDescriptorSetLayout();

    printf("Creating Graphics Pipeline.\n");
    createGraphicsPipeline();

    printf("Creating Command Pool.\n");
    createCommandPool();

    printf("Creating Color Resources.\n");
    createColorResources();
    printf("Creating Depth Resources.\n");
    createDepthResources();
    printf("Creating Framebuffers.\n");
    createFramebuffers();

    SDL_Event e;
    g_is_running = true;
    while (g_is_running){
        while (SDL_PollEvent(&e)){
            handleInput(e);
        }
    }

    /*
     * CLEANUP Code
     */
    vkDestroyCommandPool        (g_device, g_command_pool          , NULL); g_command_pool          = VK_NULL_HANDLE;
    vkDestroyPipeline           (g_device, g_graphics_pipeline     , NULL); g_graphics_pipeline     = VK_NULL_HANDLE;
    vkDestroyPipelineLayout     (g_device, g_pipeline_layout       , NULL); g_pipeline_layout       = VK_NULL_HANDLE;
    vkDestroyDescriptorSetLayout(g_device, g_descriptor_set_layout , NULL); g_descriptor_set_layout = VK_NULL_HANDLE;
    vkDestroyRenderPass         (g_device, g_render_pass           , NULL); g_render_pass           = VK_NULL_HANDLE;
    vkDestroyImage              (g_device, g_color_image           , NULL); g_color_image           = VK_NULL_HANDLE;
    vkDestroyImageView          (g_device, g_color_image_view      , NULL); g_color_image_view      = VK_NULL_HANDLE;
    vkFreeMemory                (g_device, g_color_image_memory    , NULL); g_color_image_memory    = VK_NULL_HANDLE;
    vkDestroyImage              (g_device, g_depth_image           , NULL); g_depth_image           = VK_NULL_HANDLE;
    vkDestroyImageView          (g_device, g_depth_image_view      , NULL); g_depth_image_view      = VK_NULL_HANDLE;
    vkFreeMemory                (g_device, g_depth_image_memory    , NULL); g_depth_image_memory    = VK_NULL_HANDLE;

    for (size_t i = 0; i < g_num_swap_chain_images; i++) vkDestroyImageView(g_device, g_swap_chain_image_views[i], NULL);
    free(g_swap_chain_image_views); g_swap_chain_image_views = NULL;

    for (size_t i = 0; i < g_num_swap_chain_framebuffers; i++) vkDestroyFramebuffer(g_device, g_swap_chain_framebuffers[i], NULL);
    free(g_swap_chain_framebuffers); g_swap_chain_framebuffers = NULL;

    free(g_swap_chain_images); g_swap_chain_images = NULL;

    vkDestroySwapchainKHR(g_device, g_swap_chain, NULL); g_swap_chain = VK_NULL_HANDLE;
    vkDestroyDevice      (g_device, NULL); g_device = VK_NULL_HANDLE;

    vkDestroySurfaceKHR(g_instance, g_surface, NULL); g_surface = VK_NULL_HANDLE;

    const PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)
        (vkGetInstanceProcAddr(g_instance, "vkDestroyDebugUtilsMessengerEXT"));

    if (func != NULL) {
        func(g_instance, g_debug_messenger, NULL);
        g_debug_messenger = VK_NULL_HANDLE;
    }

    vkDestroyInstance(g_instance, NULL); g_instance = VK_NULL_HANDLE;

    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }
    SDL_Quit();

    return EXIT_SUCCESS;
}