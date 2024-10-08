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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <arm/limits.h>
#include <sys/stat.h>

#include <cglm/cglm.h>
#include <cglm/quat.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tinyobj_loader_c.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define DEFAULT_WINDOW_WIDTH 800
#define DEFAULT_WINDOW_HEIGHT 600
#define PROJECT_NAME "Vulkan Engine"

#define NUM_MODELS 2

#define ENABLE_VALIDATION_LAYERS true
#define ALLOW_DEVICE_WITHOUT_INTEGRATED_GPU true
#define ALLOW_DEVICE_WITHOUT_GEOMETRY_SHADER true

#define UINT32_UNINITIALIZED_VALUE UINT32_MAX
#define UINT32_INVALIDED_VALUE 0xDEADBEEF

#define NO_TIMEOUT UINT64_MAX;

#define REQUIRED_VULKAN_API_VERSION VK_API_VERSION_1_3

#define REQUIRED_DEVICE_EXTENSIONS {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME}

#define MAX_FRAMES_IN_FLIGHT 2

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define quat vec4

const float PI = M_PI;
const float PI_2 = 2.0f * M_PI;
const float PI_HALF = M_PI / 2.0f;
const float PI_QUARTER = M_PI / 4.0f;
const float PI_DEG = 90.0f;

const float CAMERA_MAX_PITCH = 50.0f;

const float CLIPPING_PLANE_NEAR = 0.1f;
const float CLIPPING_PLANE_FAR = 100.0f;

/* DEBUG FEATURE FLAGS */
#define MALLOC_DEBUG true
#define REALLOC_DEBUG true
#define FREE_DEBUG true
#define SCOPE_TIMER_ENABLED true

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

    if(elapsed < 1.0) {
        printf("[[DS-SCOPE_TIMER]] %s | File: %s | Line: %d | Function: %s | Elapsed time: %.3f milliseconds\n",
                                   t->info, t->file, t->line, t->func, elapsed * 1000.0);
    } else {
        printf("[[DS-SCOPE_TIMER]] %s | File: %s | Line: %d | Function: %s | Elapsed time: %.3f seconds\n",
                                   t->info, t->file, t->line, t->func, elapsed);
    }
}

void timer_cleanup(const Timer* t) { stop_timer(t); }

#define SCOPE_TIMER __attribute__((cleanup(timer_cleanup))) \
Timer _timer_instance; \
start_timer(&_timer_instance, __FILE__, __LINE__, __func__);

// PANIC macro to print error details (with file, line, and function info) and abort
// While not very clean I am explicitly fine with memory leaks when PANIC is called during the initialization
// as the program gets terminated, might clean that up later on, maybe build some custom unique_ptr setup for the initialization.
#define PANIC(fmt, ...) \
    do { \
        fprintf(stderr, "[[DS-PANIC]] function %s (file: %s, line: %d): ", __func__, __FILE__, __LINE__); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
        fprintf(stderr, "\n"); \
        abort(); \
    } while(0)

// This is a workaround to the fact that
// const char* err_msg = "asd";
// PANIC(err_msg)
// does not work, we need ot call PANIC("%s", err_msg) instead which is exactly what this new macro does.
#define PANIC_STR(msg) PANIC("%s", msg)

#define PANIC_NOT_IMPLEMENTED(msg) PANIC("NOT_IMPLEMENTED");

// If we are in debug mode (i.e., when NDEBUG is false), we use this malloc/realloc/free with metadata
#ifndef NDEBUG
    // Redefine malloc, realloc, and free macros to include file, line, function, and variable name metadata
    #define malloc(size) debug_malloc(size, __FILE__, __LINE__, __func__)
    #define realloc(ptr, size) debug_realloc(ptr, size, __FILE__, __LINE__, __func__)
    #define free(ptr) debug_free(ptr, #ptr, __FILE__, __LINE__, __func__)

    // Debug malloc function with metadata
    void* debug_malloc(const size_t size, const char* file, int line, const char* func) {
        printf("[[DS-MEMORY]] malloc(size=%zu) called from file: %s, line: %d, function: %s, ", size, file, line, func);

        #undef malloc
        void* ptr = malloc(size);  // Call the real malloc
        #define malloc(size) debug_malloc(size, __FILE__, __LINE__, __func__)

        if(ptr == NULL) PANIC("[[DS-MEMORY]] Failed to allocate %zu bytes in file %s, line %d, function %s", size, file, line, func);
        printf("Pointer allocated at: %p\n", ptr);
        return ptr;
    }

    // Debug realloc function with metadata
    void* debug_realloc(void* ptr, const size_t size, const char* file, int line, const char* func) {
        printf("[[DS-MEMORY]] realloc(ptr=%p, size=%zu) called from file: %s, line: %d, function: %s, ", ptr, size, file, line, func);

        #undef realloc
        void* new_ptr = realloc(ptr, size);  // Call the real realloc
        #define realloc(ptr, size) debug_realloc(ptr, size, __FILE__, __LINE__, __func__)

        if(new_ptr == NULL) PANIC("[[DS-MEMORY]] Failed to reallocate %zu bytes in file %s, line %d, function %s", size, file, line, func);
        printf("Pointer reallocated at: %p\n", new_ptr);
        return new_ptr;
    }

    // Debug free function with metadata and variable name
    void debug_free(void* ptr, const char* var_name, const char* file, int line, const char* func) {
        printf("[[DS-MEMORY]] free(ptr=%p, variable=%s) called from file: %s, line: %d, function: %s\n", ptr, var_name, file, line, func);

        #undef free
        free(ptr);  // Call the real free
        #define free(ptr) debug_free(ptr, #ptr, __FILE__, __LINE__, __func__)
    }
#endif

// Function to check if the file is a regular file using stat
int is_regular_file(const char *filename) {
    struct stat fileStat;
    if(stat(filename, &fileStat) != 0) return 0; // File does not exist or is in an error state
    return S_ISREG(fileStat.st_mode); // Check if it's a regular file
}

bool file_exists(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    if(file) {
        fclose(file);
        return true;
    }
    return false;
}

//@DS:NEEDS_FREE_AFTER_USE
char *readFile(const char *filename, size_t *out_size) {
    // First check if the file exists and is a regular file
    if(!is_regular_file(filename)) {
        fprintf(stderr, "Error: '%s' is not a regular file or does not exist.\n", filename);
        return NULL;
    }

    FILE *file = fopen(filename, "rb");
    if(!file) {
        fprintf(stderr, "Error: Unable to open file '%s': %s\n", filename, strerror(errno));
        return NULL;
    }

    // Seek to the end to determine the file size
    if(fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "Error: Unable to seek to the end of file '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    const long fileSize = ftell(file);
    if(fileSize == -1L) {
        fprintf(stderr, "Error: Unable to get file size of '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    if(fileSize > LONG_MAX) {
        fprintf(stderr, "Error: File size exceeds maximum supported size.\n");
        fclose(file);
        return NULL;
    }

    *out_size = (size_t)fileSize;

    // Go back to the beginning of the file
    rewind(file);

    // Allocate buffer for the file content
    char *buffer = malloc(*out_size);
    if(!buffer) {
        fprintf(stderr, "Error: Memory allocation failed for file '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    // Read the file into the buffer
    const size_t bytesRead = fread(buffer, 1, *out_size, file);
    if(bytesRead != *out_size) {
        fprintf(stderr, "Error: Unable to read entire file '%s'\n", filename);
        free(buffer);
        fclose(file);
        return NULL;
    }

    fclose(file);
    return buffer;
}

typedef struct {
    vec3 pos;
    vec3 normal;
    vec2 texCoord;
} Vertex;

typedef struct {
    vec3 position    __attribute__((aligned(16)));
    quat rotation;
    vec3 scale       __attribute__((aligned(16)));
} Transform;

typedef struct {
    mat4 model;
    mat4 view;
    mat4 proj;
} UniformBufferObject ;

UniformBufferObject UniformBufferObject_create(mat4 model, mat4 view, mat4 proj) {
    UniformBufferObject ubo;
    glm_mat4_copy(model, ubo.model);
    glm_mat4_copy(view, ubo.view);
    glm_mat4_copy(proj, ubo.proj);
    return ubo;
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

VkCommandBuffer* g_command_buffers;
uint32_t g_num_command_buffers;

VkSampleCountFlagBits g_MSAASamples;

VkQueue g_graphics_queue = VK_NULL_HANDLE;
VkQueue g_presentation_queue = VK_NULL_HANDLE;

VkDebugUtilsMessengerEXT g_debug_messenger;

bool g_is_running = false;

uint32_t g_mip_levels = UINT32_UNINITIALIZED_VALUE;
VkImage g_texture_image;
VkDeviceMemory g_texture_image_memory;
VkImageView g_texture_image_view;
VkSampler g_texture_sampler;

VkBuffer* g_uniform_buffers;
VkDeviceMemory* g_uniform_buffers_memory;
void** g_uniform_buffers_mapped;

VkDescriptorPool g_descriptor_pool;
VkDescriptorSet* g_descriptor_sets;
uint32_t g_num_descriptor_sets;

VkSemaphore* g_image_available_semaphores;
uint32_t g_num_image_available_semaphores;

VkSemaphore* g_render_finished_semaphores;
uint32_t g_num_render_finished_semaphores;

VkFence* g_in_flight_fences;
uint32_t g_num_in_flight_fences;

uint32_t g_current_frame_idx; // 0 <= m_CurrentFrameIdx < Max Frames in Flight
uint32_t g_frame_counter;    // How many frames have been rendered in total

PushConstants g_push_constants;

int g_rendering_stage = 3;

bool g_did_framebuffer_resize = false;

vec3 g_camera_eye = {2.0f, 4.0f, 2.0f};
vec3 g_camera_center = {0.0f, 0.0f, 0.0f};
vec3 g_camera_up = {0.0f, 0.0f, 1.0f};

clock_t g_start_time;

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    // ReSharper disable once CppParameterMayBeConst
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    // ReSharper disable once CppParameterMayBeConst
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void *pUserData
) {
    (void)messageType; (void)pUserData; // Suppressed "Unused Parameter" warning
    if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) printf("[[VK-Validation_INFO]] %s\n", pCallbackData->pMessage);
    else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) fprintf(stderr, "[[VK-Validation_ERROR]] %s\n", pCallbackData->pMessage);
    else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) printf("[[VK-Validation_WARNING]] %s\n", pCallbackData->pMessage);
    else if(messageSeverity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) printf("[[VK-Validation_VERBOSE]] %s\n", pCallbackData->pMessage);
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
    } else { create_info.enabledLayerCount = 0; create_info.pNext = NULL; }

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
    if(details == NULL) return;
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
    if(counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
    if(counts & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
    if(counts & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
    if(counts & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
    if(counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if(counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
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

    if(vkCreateDevice(g_physical_device, &createInfo, NULL, &g_device) != VK_SUCCESS) PANIC("Failed to create device.");

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
    if(!isExtentUndefined) return capabilities->currentExtent;

    int width = 0; int height = 0;
    SDL_Vulkan_GetDrawableSize(g_window, &width, &height);
    uint32_t clampedWidth = width;
    if(clampedWidth < capabilities->minImageExtent.width)
        clampedWidth = capabilities->minImageExtent.width;
    if(clampedWidth > capabilities->maxImageExtent.width)
        clampedWidth = capabilities->maxImageExtent.width;

    uint32_t clampedHeight = height;
    if(clampedHeight < capabilities->minImageExtent.height)
        clampedHeight = capabilities->minImageExtent.height;
    if(clampedHeight > capabilities->maxImageExtent.height)
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
    if(vkCreateImageView(g_device, &viewInfo, NULL, &imageView) != VK_SUCCESS) PANIC("Failed to create texture image view!");
    return imageView;
}

void createTextureImageView() {
    g_texture_image_view = createImageView(g_texture_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, g_mip_levels);
}

void createTextureSampler() {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(g_physical_device, &properties);

    const VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0,
        .maxLod = (float)g_mip_levels,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE};

    if (vkCreateSampler(g_device, &samplerInfo, NULL, &g_texture_sampler) != VK_SUCCESS) PANIC("failed to create texture sampler!");
}

void createSwapChain() {
    SwapChainSupportDetails details;
    querySwapChainSupport(g_physical_device, &details);

    const VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(details.formats, details.num_formats);
    const VkPresentModeKHR presentMode = chooseSwapPresentMode(details.present_modes, details.num_present_modes);
    const VkExtent2D extent = chooseSwapExtent(&details.capabilities);

    // Limit the number of swap chain images to MAX_FRAMES_IN_FLIGHT
    g_num_swap_chain_images = MAX_FRAMES_IN_FLIGHT;

    // Ensure imageCount is within the allowed range
    if(g_num_swap_chain_images < details.capabilities.minImageCount) g_num_swap_chain_images = details.capabilities.minImageCount;
    if(g_num_swap_chain_images > details.capabilities.maxImageCount) g_num_swap_chain_images = details.capabilities.maxImageCount;
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

    if(queue_family_indices.graphicsFamily != queue_family_indices.presentationFamily) {
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

        if(tiling == VK_IMAGE_TILING_LINEAR) {
            if((props.linearTilingFeatures & features) == features) {
                return format;
            }
        } else if(tiling == VK_IMAGE_TILING_OPTIMAL) {
            if((props.optimalTilingFeatures & features) == features) {
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

    if(vkCreateRenderPass(g_device, &renderPassInfo, NULL, &g_render_pass) != VK_SUCCESS) PANIC("failed to create render pass!");
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

    if(vkCreateDescriptorSetLayout(g_device, &layoutInfo, NULL, &g_descriptor_set_layout) != VK_SUCCESS) PANIC("failed to create descriptor set layout!");
}

VkShaderModule createShaderModule(const char* code, size_t code_length) {
    const VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = (uint32_t)code_length,
        .pCode = (const uint32_t *)code
    };
    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if(vkCreateShaderModule(g_device, &createInfo, NULL, &shaderModule) != VK_SUCCESS) PANIC("Failed to create shader!");
    return shaderModule;
}


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
    if(!vertShaderCode) PANIC("Could not read vertex shader file.");
    // ReSharper disable once CppDFAMemoryLeak
    const char* fragShaderCode = readFile("shaders/compiled/shader_phong_stages.frag.spv", &fragShaderCode_length);
    if(!fragShaderCode) PANIC("Could not read fragment shader file.");

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

    if(vkCreatePipelineLayout(g_device, &pipelineLayoutInfo, NULL, &g_pipeline_layout) != VK_SUCCESS) PANIC("failed to create pipeline layout!");

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

    if(vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &g_graphics_pipeline) != VK_SUCCESS) PANIC("failed to create graphics pipeline!");

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

    if(vkCreateCommandPool(g_device, &create_info, NULL, &g_command_pool) != VK_SUCCESS) PANIC("Failed to create command pool!");
}

uint32_t findMemoryType(const uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(g_physical_device, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
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

    if(vkCreateImage(g_device, &imageInfo, NULL, image) != VK_SUCCESS)
        PANIC("failed to create image!");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(g_device, *image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if(vkAllocateMemory(g_device, &allocInfo, NULL, imageMemory) != VK_SUCCESS)
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
        if(vkCreateFramebuffer(g_device, &framebufferInfo, NULL, &g_swap_chain_framebuffers[i]) != VK_SUCCESS) PANIC("failed to create framebuffer!");
    }
}

uint32_t calculate_mip_levels(const uint32_t width, const uint32_t height) {
    uint32_t max_dim = MAX(width, height);
    uint32_t mip_levels = 0;
    while (max_dim > 0) {
        mip_levels++;
        max_dim /= 2;
    }
    return mip_levels;
}

VkCommandBuffer beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(g_device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer};

    vkQueueSubmit(g_graphics_queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(g_graphics_queue);

    vkFreeCommandBuffers(g_device, g_command_pool, 1, &commandBuffer);
}

void createBuffer(
    const VkDeviceSize size,
    const VkBufferUsageFlags usage,
    const VkMemoryPropertyFlags properties,
    VkBuffer *buffer,
    VkDeviceMemory *bufferMemory)
{
    const VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

    if(vkCreateBuffer(g_device, &bufferInfo, NULL, buffer) != VK_SUCCESS) PANIC("failed to create buffer!");

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(g_device, *buffer, &memRequirements);

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)};

    if(vkAllocateMemory(g_device, &allocInfo, NULL, bufferMemory) != VK_SUCCESS) PANIC("failed to allocate buffer memory!");
    vkBindBufferMemory(g_device, *buffer, *bufferMemory, 0);
}

void copyBufferToImage(VkBuffer buffer, VkImage image, const uint32_t width, const uint32_t height) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    const VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .imageOffset = {.x = 0, .y = 0, .z = 0},
        .imageExtent = {.width = width, .height = height, .depth = 1}};

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);

    endSingleTimeCommands(commandBuffer);
}


void transitionImageLayout(
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    const uint32_t mipLevels)
{
    (void)format; // Suppresses compiler warnings
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = 0,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkPipelineStageFlags sourceStage = 0;
    VkPipelineStageFlags destinationStage = 0;
    if(oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // Transition from an undefined layout to a layout that allows for writing by transfer operations
        barrier.srcAccessMask = 0;                            // No need to wait for anything
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // The image will be written to

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    } else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // Transition from a transfer destination layout to a shader-readable layout
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // Must wait for the transfer to complete
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;    // The image will be read by the shader

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    } else if(oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        // Transition from transfer source to presentation layout
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; // Must wait for transfer reads to complete
        barrier.dstAccessMask = 0;                           // No need for further synchronization before presenting

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    } else if(oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        // Transition from presentation layout to transfer source layout
        barrier.srcAccessMask = 0;                           // No need to wait for anything before transitioning
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; // The image will be read as a source for transfer

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    } else { PANIC("unsupported layout transition!"); }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, NULL,
        0, NULL,
        1, &barrier);

    endSingleTimeCommands(commandBuffer);
}


void generateMipmaps(
    VkImage image,
    VkFormat imageFormat,
    const int32_t texWidth,
    const int32_t texHeight,
    const uint32_t mipLevels)
{
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(g_physical_device, imageFormat, &formatProperties);

    if(!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        PANIC("texture image format does not support linear blitting!");
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1},
    };

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            NULL,
            0,
            NULL,
            1,
            &barrier);

        VkImageBlit blit = {
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = i - 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .srcOffsets = {
                { .x = 0, .y = 0, .z = 0 },
                { .x = mipWidth, .y = mipHeight, .z = 1 }
            },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = i,
                .baseArrayLayer = 0,
                .layerCount = 1
            },
            .dstOffsets = {
                { .x = 0, .y = 0, .z = 0 },
                { .x = (mipWidth > 1 ? mipWidth / 2 : 1), .y = (mipHeight > 1 ? mipHeight / 2 : 1), .z = 1 }
            }
        };

        vkCmdBlitImage(
            commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            NULL,
            0,
            NULL,
            1,
            &barrier);

        if(mipWidth > 1) mipWidth /= 2;
        if(mipHeight > 1) mipHeight /= 2;
    }

    // Need to specifically handle the last mipMap, as the don't blit from it that is not handled in the loop
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        NULL,
        0,
        NULL,
        1,
        &barrier);

    endSingleTimeCommands(commandBuffer);
}

void createTextureImage() {
    const char* texture_fp = "./assets/textures/painted_plaster_diffuse.png";
    if(!file_exists(texture_fp)) PANIC("Texture file not found at '%s'", texture_fp);

    int texWidth = 0;
    int texHeight = 0;
    int texChannels = 0;
    stbi_uc *pixels = stbi_load(texture_fp, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if(!pixels) PANIC("Failed to load texture image!");
    VkDeviceSize imageSize = texWidth * texHeight * 4;

    // m_MipLevels = How often we can divide max(width, height) by 2, could also take the ceil here instead of floor + 1
    g_mip_levels = calculate_mip_levels(texWidth, texHeight);

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    createBuffer(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &stagingBuffer,
        &stagingBufferMemory);

    void *data = NULL;
    vkMapMemory(g_device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, imageSize);

    vkUnmapMemory(g_device, stagingBufferMemory);
    stbi_image_free(pixels);

    createImage(
        texWidth,
        texHeight,
        g_mip_levels,
        VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &g_texture_image,
        &g_texture_image_memory);
    transitionImageLayout(
        g_texture_image,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        g_mip_levels);
    copyBufferToImage(
        stagingBuffer,
        g_texture_image,
        texWidth,
        texHeight);

    vkDestroyBuffer(g_device, stagingBuffer, NULL);
    vkFreeMemory(g_device, stagingBufferMemory, NULL);

    generateMipmaps(g_texture_image, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, g_mip_levels);
}

void createUniformBuffers() {
    const size_t num_models = NUM_MODELS;
    const size_t total_buffers = MAX_FRAMES_IN_FLIGHT * num_models;

    g_uniform_buffers = malloc(total_buffers * sizeof(VkBuffer));
    g_uniform_buffers_memory = malloc(total_buffers * sizeof(VkDeviceMemory));
    g_uniform_buffers_mapped = malloc(total_buffers * sizeof(void*));

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        for (size_t j = 0; j < num_models; j++) {
            const VkDeviceSize buffer_size = sizeof(UniformBufferObject);
            const size_t bufferIndex = i * num_models + j;
            createBuffer(
                buffer_size,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &g_uniform_buffers[bufferIndex],
                &g_uniform_buffers_memory[bufferIndex]
            );
            const VkResult result = vkMapMemory(
                g_device,
                g_uniform_buffers_memory[bufferIndex],
                0,
                buffer_size,
                0,
                &g_uniform_buffers_mapped[bufferIndex]
            );
            if(result != VK_SUCCESS) PANIC("failed to map uniform buffer memory!");
        }
    }
}

void cleanupUniformBuffers() {
    const size_t num_models = NUM_MODELS;
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        for (size_t j = 0; j < num_models; j++) {
            const size_t buffer_index = i * num_models + j;
            vkDestroyBuffer(g_device, g_uniform_buffers[buffer_index], NULL);
            vkFreeMemory(g_device, g_uniform_buffers_memory[buffer_index], NULL);
        }
    }
}

void createDescriptorPool() {
    const size_t num_models = NUM_MODELS;
    const size_t total_sets = MAX_FRAMES_IN_FLIGHT * num_models;

    VkDescriptorPoolSize poolSizes[] = {
        { .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER        , .descriptorCount = (uint32_t)total_sets },
        { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = (uint32_t)total_sets }
    };

    const VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes,
        .maxSets = (uint32_t)total_sets};

    if (vkCreateDescriptorPool(g_device, &poolInfo, NULL, &g_descriptor_pool) != VK_SUCCESS) PANIC("failed to create descriptor pool!");
}

void createDescriptorSets() {
    const size_t numModels = NUM_MODELS;
    const size_t total_sets = MAX_FRAMES_IN_FLIGHT * numModels;

    VkDescriptorSetLayout* layouts = malloc(total_sets * sizeof(VkDescriptorSetLayout));
    for(size_t i = 0; i < total_sets; i++) layouts[i] = g_descriptor_set_layout;

    const VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = g_descriptor_pool,
        .descriptorSetCount = (uint32_t)total_sets,
        .pSetLayouts = layouts};

    g_num_descriptor_sets = total_sets;
    g_descriptor_sets = malloc(g_num_descriptor_sets * sizeof(VkDescriptorSet));
    if (vkAllocateDescriptorSets(g_device, &allocInfo, g_descriptor_sets) != VK_SUCCESS) PANIC("failed to allocate descriptor sets!");
    free(layouts);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        for (size_t j = 0; j < numModels; j++) {
            const size_t bufferIndex = i * numModels + j;
            VkDescriptorBufferInfo bufferInfo = {
                .buffer = g_uniform_buffers[bufferIndex],
                .offset = 0,
                .range = sizeof(UniformBufferObject)};

            VkDescriptorImageInfo imageInfo = {
                .sampler = g_texture_sampler,
                .imageView = g_texture_image_view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

            VkWriteDescriptorSet descriptorWrites[2];
            descriptorWrites[0] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = g_descriptor_sets[bufferIndex],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &bufferInfo};
            descriptorWrites[1] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = g_descriptor_sets[bufferIndex],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfo};

            vkUpdateDescriptorSets(g_device, 2, descriptorWrites, 0, NULL);
        }
    }
}

void createCommandBuffers() {
    g_num_command_buffers = MAX_FRAMES_IN_FLIGHT;
    g_command_buffers = malloc(g_num_command_buffers * sizeof(VkCommandBuffer));

    const VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = g_command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 2};

    if (vkAllocateCommandBuffers(g_device, &allocInfo, g_command_buffers) != VK_SUCCESS) PANIC("failed to allocate command buffers!");
}

void createSyncObjects() {
    g_num_image_available_semaphores = MAX_FRAMES_IN_FLIGHT;
    g_num_render_finished_semaphores = MAX_FRAMES_IN_FLIGHT;
    g_num_in_flight_fences = MAX_FRAMES_IN_FLIGHT;

    g_image_available_semaphores = malloc(g_num_image_available_semaphores * sizeof(VkSemaphore));
    g_render_finished_semaphores = malloc(g_num_render_finished_semaphores * sizeof(VkSemaphore));
    g_in_flight_fences = malloc(g_num_in_flight_fences * sizeof(VkFence));

    const VkSemaphoreCreateInfo semaphoreInfo = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    const VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT};

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        fprintf(stdout, "\t%zu. frame\n", i + 1);
        const VkResult result_1 = vkCreateSemaphore(g_device, &semaphoreInfo, NULL, &g_image_available_semaphores[i]);
        if (result_1 != VK_SUCCESS) PANIC("failed to create ImageAvailable semaphore!");
        const VkResult result_2 = vkCreateSemaphore(g_device, &semaphoreInfo, NULL, &g_render_finished_semaphores[i]);
        if (result_2 != VK_SUCCESS) PANIC("failed to create RenderFinished semaphore!");
        const VkResult result_3 = vkCreateFence(g_device, &fenceInfo, NULL, &g_in_flight_fences[i]);
        if (result_3 != VK_SUCCESS) PANIC("failed to create InFlight fence!");
    }
}

void recreateSwapChain() {
    PANIC("NOT_IMPLEMENTED");
    /*
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_Window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_Window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_Device);

    cleanupSwapChain();

    createSwapChain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createColorResources();
    createDepthResources();
    createFramebuffers();

    // Recreate uniform buffers and descriptor sets
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();

    // No need to recreate command buffers if you reset them each frame
    */
}

void updatePushConstants() {
    memcpy(g_push_constants.cameraCenter, g_camera_center, sizeof(vec3));
    memcpy(g_push_constants.cameraEye, g_camera_eye, sizeof(vec3));
    memcpy(g_push_constants.cameraUp, g_camera_up, sizeof(vec3));
    g_push_constants.stage = g_rendering_stage;

    // Get current time using clock() from <time.h>
    const clock_t current_time = clock();

    // Compute delta time in seconds (clock() returns time in clock ticks)
    const float delta_time = (float)(current_time - g_start_time) / CLOCKS_PER_SEC;

    // Store deltaTime in push constants
    g_push_constants.time = delta_time;
}

void record_command_buffers(VkCommandBuffer commandBuffer, const uint32_t imageIndex) {
    const VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) PANIC("failed to begin recording command buffer!");

    #define num_clear_values 2
    VkClearValue clearValues[num_clear_values];
    clearValues[0].color.float32[0] = 0.0f;
    clearValues[0].color.float32[1] = 0.0f;
    clearValues[0].color.float32[2] = 0.0f;
    clearValues[0].color.float32[3] = 1.0f;
    clearValues[1].depthStencil.depth = 1.0f;
    clearValues[1].depthStencil.stencil = 0;

    const VkRenderPassBeginInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = g_render_pass,
        .framebuffer = g_swap_chain_framebuffers[imageIndex],
        .renderArea = {.offset = {0, 0}, .extent = g_swap_chain_extent},
        .clearValueCount = (uint32_t)num_clear_values,
        .pClearValues = clearValues};
    #undef num_clear_values

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)g_swap_chain_extent.width,
        .height = (float)>g_swap_chain_extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = g_swap_chain_extent};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    updatePushConstants();
    vkCmdPushConstants(
        commandBuffer,
        g_pipeline_layout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(PushConstants),
        &g_push_constants);

    for (size_t j = 0; j < 2; j++) {
        const size_t descriptorSetIndex = g_current_frame_idx * NUM_MODELS + j;
        VkDescriptorSet descriptorSet = g_descriptor_sets[descriptorSetIndex];

        if (descriptorSet == VK_NULL_HANDLE) PANIC("Invalid descriptor set handle!");

        m_Models[j]->enqueueIntoCommandBuffer(commandBuffer, descriptorSet);
    }
    vkCmdEndRenderPass(commandBuffer);
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) PANIC("failed to record command buffer!");
}

// Function to create the UBO
UniformBufferObject get_UBO() {
    if (g_start_time == 0) g_start_time = clock();
    clock_t current_time = clock();
    float delta_time = (float)(current_time - g_start_time) / CLOCKS_PER_SEC;

    mat4 view;
    glm_lookat(g_camera_eye, g_camera_center, g_camera_up, view);

    mat4 proj;
    glm_perspective(PI_QUARTER, (float)(g_swap_chain_extent.width) / (float)(g_swap_chain_extent.height),
                    CLIPPING_PLANE_NEAR, CLIPPING_PLANE_FAR, proj);
    proj[1][1] *= -1; // Vulkan and OpenGL have opposite y orientation, and (c)glm is mainly written for openGL

    mat4 model_matrix;
    glm_mat4_identity(model_matrix);

    return UniformBufferObject_create(model_matrix, view, proj);
}


void drawFrame() {
    vkWaitForFences(g_device, 1, &g_in_flight_fences[g_current_frame_idx], VK_TRUE, NO_TIMEOUT);

    uint32_t imageIndex = 0;
    const VkResult resultNextImage = vkAcquireNextImageKHR(
        g_device,
        g_swap_chain,
        NO_TIMEOUT,
        g_image_available_semaphores[g_current_frame_idx],
        VK_NULL_HANDLE,
        &imageIndex);

    if (resultNextImage == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    if (resultNextImage != VK_SUCCESS && resultNextImage != VK_SUBOPTIMAL_KHR) PANIC("failed to acquire swap chain image!");

    vkResetFences(g_device, 1, &g_in_flight_fences[g_current_frame_idx]);

    vkResetCommandBuffer(g_command_buffers[g_current_frame_idx], 0);
    record_command_buffers(g_command_buffers[g_current_frame_idx], imageIndex);

    for (size_t i = 0; i < 2; i++) {
        UniformBufferObject ubo = get_UBO();
        const size_t bufferIndex = g_current_frame_idx * 2 + i;
        memcpy(g_uniform_buffers_mapped[bufferIndex], &ubo, sizeof(ubo));
    }

    VkSemaphore waitSemaphores[] = {g_image_available_semaphores[g_current_frame_idx]};
    VkPipelineStageFlags waitStages[] = {(VkPipelineStageFlags)(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)};
    VkSemaphore signalSemaphores[] = {g_render_finished_semaphores[g_current_frame_idx]};
    const VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = waitSemaphores,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &g_command_buffers[g_current_frame_idx],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signalSemaphores};

    if (vkQueueSubmit(g_graphics_queue, 1, &submitInfo, g_in_flight_fences[g_current_frame_idx]) != VK_SUCCESS) PANIC("failed to submit draw command buffer!");

    VkSwapchainKHR swapChains[] = {g_swap_chain};
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signalSemaphores,
        .swapchainCount = 1,
        .pSwapchains = swapChains,
        .pImageIndices = &imageIndex,
        .pResults = NULL};

    const VkResult resultQueue = vkQueuePresentKHR(g_presentation_queue, &presentInfo);
    if (resultQueue == VK_ERROR_OUT_OF_DATE_KHR || resultQueue == VK_SUBOPTIMAL_KHR || g_did_framebuffer_resize) {
        g_did_framebuffer_resize = false;
        recreateSwapChain();
    } else if (resultQueue != VK_SUCCESS) {
        PANIC("failed to present swap chain image!");
    }

    g_current_frame_idx = (g_current_frame_idx + 1) % MAX_FRAMES_IN_FLIGHT;
    g_frame_counter += 1;
}

int main() {
    /*
     * Start of Initialization
     */
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

    printf("Creating Texture image.\n");
    createTextureImage();
    printf("Creating Texture image View.\n");
    createTextureImageView();
    printf("Creating Texture Sampler\n");
    createTextureSampler();

    printf("Instantiating Models!\n");
    Transform torus_transform = {
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}};

    Transform sphere_transform = {
        {3.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}};
    printf("Successfully instantiated Models!\n");

    createUniformBuffers();

    createDescriptorPool();
    createDescriptorSets();

    createCommandBuffers();
    createSyncObjects();
    /*
     * End of Initialization
     */

    SDL_Event e;
    g_is_running = true;
    while (g_is_running){
        while (SDL_PollEvent(&e)){
            handleInput(e);
        }
        drawFrame();
    }

    /*
     * CLEANUP Code
     */
    for(size_t i = 0; i < g_num_image_available_semaphores; i++) vkDestroySemaphore(g_device, g_image_available_semaphores[i], NULL);
    for(size_t i = 0; i < g_num_render_finished_semaphores; i++) vkDestroySemaphore(g_device, g_render_finished_semaphores[i], NULL);
    for(size_t i = 0; i < g_num_in_flight_fences; i++) vkDestroyFence(g_device, g_in_flight_fences[i], NULL);
    free(g_image_available_semaphores); free(g_render_finished_semaphores); free(g_in_flight_fences);

    vkFreeCommandBuffers(g_device, g_command_pool, g_num_command_buffers, g_command_buffers);
    free(g_command_buffers); g_command_buffers = VK_NULL_HANDLE;

    free(g_descriptor_sets); g_descriptor_sets = VK_NULL_HANDLE;
    vkDestroyDescriptorPool(g_device, g_descriptor_pool, NULL);

    cleanupUniformBuffers();

    vkDestroySampler(g_device, g_texture_sampler, NULL); g_texture_sampler = VK_NULL_HANDLE;
    vkDestroyImageView(g_device, g_texture_image_view, NULL); g_texture_image_view = VK_NULL_HANDLE;
    vkFreeMemory(g_device, g_texture_image_memory, NULL); g_texture_image_memory = VK_NULL_HANDLE;
    vkDestroyImage(g_device, g_texture_image, NULL); g_texture_image = VK_NULL_HANDLE;

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

    if(func != NULL) {
        func(g_instance, g_debug_messenger, NULL);
        g_debug_messenger = VK_NULL_HANDLE;
    }

    vkDestroyInstance(g_instance, NULL); g_instance = VK_NULL_HANDLE;

    if(g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }
    SDL_Quit();
    printf("Shut down SDL.\n");

    printf("Program finished running, Goodbye!\n");
    return EXIT_SUCCESS;
}