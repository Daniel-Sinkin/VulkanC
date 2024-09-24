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

SDL_Window* g_window;
VkInstance g_instance;

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

bool initWindow() {
    printf("Trying to initialize window.\n");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        return false;
    }

    if (!SDL_Vulkan_LoadLibrary(NULL)) {
        printf("Vulkan support is available.\n");
    } else {
        fprintf(stderr, "Vulkan support not found: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
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

    if (!g_window) {
        printf("Failed to create SDL window: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    SDL_SetWindowResizable(g_window, SDL_FALSE);

    printf("Successfully initialized window.\n");
    return true;
}

void handleInput(const SDL_Event e) {
    if (e.type == SDL_QUIT) {
        printf("Got a SLD_QUIT event!\n");
        g_is_running = false;
    }
}

/*
 * It is the callers responsibility to free extensionCount after use!
 */
const char** getRequiredExtensions(uint32_t* extensionCount) {
    unsigned int sdlExtensionCount = 0;

    if (!SDL_Vulkan_GetInstanceExtensions(NULL, &sdlExtensionCount, NULL)) {
        SDL_Log("Could not get Vulkan instance extensions: %s", SDL_GetError());
        return NULL;
    }

    const char** sdlExtensions = malloc(sdlExtensionCount * sizeof(const char*));
    if (!sdlExtensions) {
        SDL_Log("Failed to allocate memory for SDL Vulkan extensions");
        return NULL;
    }

    // Get the actual extension names
    if (!SDL_Vulkan_GetInstanceExtensions(NULL, &sdlExtensionCount, sdlExtensions)) {
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
        if(temp == NULL) {
            fprintf(stderr, "Failed to reallocate in getRequiredExtensions!");
            free(extensions);
            return NULL;
        }
        extensions = temp;
        extensions[*extensionCount] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        *extensionCount += 1;
    }

    return extensions;
}

bool checkValidationLayerSupport() {
    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);

    if(layer_count == 0) {
        fprintf(stderr, "No Instance Layers supported, so in particular no validation layers!\n");
        return false;
    }

    printf("Checking Validation Layer Support.\n");
    VkLayerProperties* availiable_layers = malloc(layer_count * sizeof(VkLayerProperties));
    vkEnumerateInstanceLayerProperties(&layer_count, availiable_layers);
    bool found = false;
    for(size_t i = 0; i < layer_count; i++) {
        if(strcmp(availiable_layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0) {
            found = true;
            break;
        }
    }
    free(availiable_layers); availiable_layers = NULL;

    if(!found) {
        fprintf(stderr, "Couldn't find the required layers.\n");
        return false;
    }
    printf("Found all necessary layers.\n");
    return true;
}

bool initInstance() {
    if(ENABLE_VALIDATION_LAYERS) {
        if(!checkValidationLayerSupport()) {
            fprintf(stderr, "Validation errors are activated but not supported!");
            return false;
        }
    }

    uint32_t apiVersion = 0;
    if(vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS) return false;
    if(apiVersion < VK_API_VERSION_1_3) return false;

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "Daniels Vulkan Engine",
        .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(0, 0, 1),
        .apiVersion = VK_API_VERSION_1_3};

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL};

    uint32_t required_extension_count;
    const char** required_extensions = getRequiredExtensions(&required_extension_count);
    if(required_extensions == NULL) {
        free(required_extensions);
        return false;
    }

#if defined(__APPLE__) && defined(__arm64__)
    const char** temp = realloc(required_extensions, (required_extension_count + 2) * sizeof(const char*));
    if(temp == NULL) {
        fprintf(stderr, "Failed to reallocate for MacOS specific extensions.");
        free(required_extensions);
        return false;
    }
    required_extensions = temp;
    required_extensions[required_extension_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    required_extensions[required_extension_count++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    create_info.enabledExtensionCount = required_extension_count;
    create_info.ppEnabledExtensionNames = required_extensions;

    uint32_t available_extension_count;
    vkEnumerateInstanceExtensionProperties(NULL, &available_extension_count, NULL);
    VkExtensionProperties* available_extensions = malloc(available_extension_count * sizeof(VkExtensionProperties));
    if(available_extensions == NULL) {
        fprintf(stderr, "Failed to allocate extension info.");
        free(required_extensions);
        return false;
    }
    vkEnumerateInstanceExtensionProperties(NULL, &available_extension_count, available_extensions);
    for(int i = 0; i < required_extension_count; i++) {
        bool found = false;
        for(int j = 0; j < available_extension_count; j++) {
            if(strcmp(required_extensions[i], available_extensions[j].extensionName) == 0) {
                found = true;
                break;
            }
        }
        if(!found) {
            fprintf(stderr, "Failed to find required extension '%s'", required_extensions[i]);
            free(available_extensions); free(required_extensions);
            return false;
        }
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
        create_info.enabledLayerCount = 0;
        create_info.pNext = NULL;
    }

    VkResult result = vkCreateInstance(&create_info, NULL, &g_instance);
    free(available_extensions); free(required_extensions);
    if(result != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan instance!");
        return false;
    }

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
            fprintf(stderr, "Failed to initialize debug utils messenger.");
        }
    }

    return true;
}

int main() {
    if(!initWindow()) return EXIT_FAILURE;
    if(!initInstance()) return EXIT_FAILURE;


    SDL_Event e;
    while (g_is_running){
        while (SDL_PollEvent(&e)){
            handleInput(e);
        }
    }

    const PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT)(vkGetInstanceProcAddr(g_instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (func != NULL) func(g_instance, g_debug_messenger, NULL);
    g_debug_messenger = NULL;
    vkDestroyInstance(g_instance, NULL);

    if(g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();

    return EXIT_SUCCESS;
}