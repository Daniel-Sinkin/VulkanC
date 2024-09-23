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

SDL_Window* g_window;

bool g_is_running = true;

// Window initialization function
void initWindow() {
    printf("Trying to initialize window.\n");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }


    if (!SDL_Vulkan_LoadLibrary(NULL)) {
        printf("Vulkan support is available.\n");
    } else {
        fprintf(stderr, "Vulkan support not found: %s\n", SDL_GetError());
        SDL_Quit();
        exit(EXIT_FAILURE);
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
        exit(EXIT_FAILURE);
    }

    SDL_SetWindowResizable(g_window, SDL_FALSE);

    printf("Successfully initialized window.\n");
}

void handleInput(const SDL_Event e) {
    if (e.type == SDL_QUIT) {
        printf("Got a SLD_QUIT event!\n");
        g_is_running = false;
    }
}

int main() {
    initWindow();

    uint32_t layer_count;
    vkEnumerateInstanceLayerProperties(&layer_count, NULL);

    if(layer_count == 0) {
        fprintf(stderr, "No Instance Layers supported, so in particular no validation layers!\n");
        return EXIT_FAILURE;
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
        return EXIT_FAILURE;
    }
    printf("Found all necessary layers.\n");



    /*
    SDL_Event e;
    while (g_is_running){
        while (SDL_PollEvent(&e)){
            handleInput(e);
        }
    }
    */

    // ReSharper disable once CppDFAConstantConditions
    if(g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();

    return EXIT_FAILURE;
}