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

#define REQUIRED_VULKAN_API_VERSION VK_API_VERSION_1_3


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
            if (new_ptr == NULL) PANIC("Failed to reallocate %zu bytes.", size);
            return new_ptr;
        }
#endif // not NDEBUG


SDL_Window* g_window;
VkInstance g_instance;

VkSurfaceKHR g_surface;

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
}

//@DS:CAN_LEAK_MEMORY
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

void pickPhysicalDevice() {
    uint32_t n_physical_devices = 0;
    vkEnumeratePhysicalDevices(g_instance, &n_physical_devices, NULL);
    if(n_physical_devices == 0) PANIC("No physical devices found!");
    VkPhysicalDevice* physical_devices = malloc(n_physical_devices * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(g_instance, &n_physical_devices, physical_devices);

    free(physical_devices);
}

int main() {
    initWindow();
    initInstance();

    if(!SDL_Vulkan_CreateSurface(g_window, g_instance, &g_surface)) PANIC("Failed to bind SDL window to VkSurface.");

    pickPhysicalDevice();

    SDL_Event e;
    while (g_is_running){
        while (SDL_PollEvent(&e)){
            handleInput(e);
        }
    }

    vkDestroySurfaceKHR(g_instance, g_surface, NULL);
    const PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)(vkGetInstanceProcAddr(g_instance, "vkDestroyDebugUtilsMessengerEXT"));
    if(func != NULL) func(g_instance, g_debug_messenger, NULL);
    g_debug_messenger = NULL;
    vkDestroyInstance(g_instance, NULL);

    if(g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();

    return EXIT_SUCCESS;
}