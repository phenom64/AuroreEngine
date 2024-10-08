module;

#include <memory>

#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_gamecontroller.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_vulkan.h>

export module sdl2;

#define ALIAS_FUNCTION(alias, name) \
    template<typename... Args> \
    auto alias(Args&&... args) -> decltype(name(std::forward<Args>(args)...)) { \
        return name(std::forward<Args>(args)...); \
    }

export namespace sdl {
    ALIAS_FUNCTION(PushEvent, SDL_PushEvent);
    ALIAS_FUNCTION(GetTicks, SDL_GetTicks);
    ALIAS_FUNCTION(GameControllerClose, SDL_GameControllerClose);
    ALIAS_FUNCTION(GetDisplayBounds, SDL_GetDisplayBounds);
    ALIAS_FUNCTION(GetError, SDL_GetError);
    ALIAS_FUNCTION(Quit, SDL_Quit);
    ALIAS_FUNCTION(Init, SDL_Init);
    ALIAS_FUNCTION(InitSubSystem, SDL_InitSubSystem);
    ALIAS_FUNCTION(QuitSubSystem, SDL_QuitSubSystem);
    ALIAS_FUNCTION(CreateWindow, SDL_CreateWindow);
    ALIAS_FUNCTION(GetDisplayName, SDL_GetDisplayName);
    ALIAS_FUNCTION(GetWindowDisplayMode, SDL_GetWindowDisplayMode);
    ALIAS_FUNCTION(NumJoysticks, SDL_NumJoysticks);
    ALIAS_FUNCTION(IsGameController, SDL_IsGameController);
    ALIAS_FUNCTION(GameControllerEventState, SDL_GameControllerEventState);
    ALIAS_FUNCTION(GameControllerOpen, SDL_GameControllerOpen);
    ALIAS_FUNCTION(GameControllerName, SDL_GameControllerName);
    ALIAS_FUNCTION(GameControllerGetJoystick, SDL_GameControllerGetJoystick);
    ALIAS_FUNCTION(JoystickInstanceID, SDL_JoystickInstanceID);
    ALIAS_FUNCTION(NumHaptics, SDL_NumHaptics);
    ALIAS_FUNCTION(HapticOpenFromJoystick, SDL_HapticOpenFromJoystick);
    ALIAS_FUNCTION(HapticRumbleInit, SDL_HapticRumbleInit);
    ALIAS_FUNCTION(HapticRumblePlay, SDL_HapticRumblePlay);
    ALIAS_FUNCTION(HapticRumbleStop, SDL_HapticRumbleStop);
    ALIAS_FUNCTION(HapticClose, SDL_HapticClose);
    ALIAS_FUNCTION(PollEvent, SDL_PollEvent);
    ALIAS_FUNCTION(WaitEvent, SDL_WaitEvent);
    ALIAS_FUNCTION(WaitEventTimeout, SDL_WaitEventTimeout);
    ALIAS_FUNCTION(GetKeyboardState, SDL_GetKeyboardState);
    ALIAS_FUNCTION(GetKeyboardFocus, SDL_GetKeyboardFocus);
    ALIAS_FUNCTION(ConvertSurfaceFormat, SDL_ConvertSurfaceFormat);
    ALIAS_FUNCTION(CreateRGBSurface, SDL_CreateRGBSurface);
    ALIAS_FUNCTION(BlitScaled, SDL_BlitScaled);
    ALIAS_FUNCTION(RWFromConstMem, SDL_RWFromConstMem);

    using Window = SDL_Window;
    using Rect = SDL_Rect;
    using DisplayMode = SDL_DisplayMode;
    using Event = SDL_Event;
    using EventType = SDL_EventType;
    using Keysym = SDL_Keysym;
    using GameController = SDL_GameController;
    using GameControllerButton = SDL_GameControllerButton;
    using GameControllerAxis = SDL_GameControllerAxis;
    using JoystickID = SDL_JoystickID;
    using PixelFormatEnum = SDL_PixelFormatEnum;
    using Surface = SDL_Surface;
    using RWops = SDL_RWops;

    constexpr auto enable = SDL_ENABLE;
    constexpr auto disable = SDL_DISABLE;

    namespace image {
        ALIAS_FUNCTION(Load, IMG_Load);
        ALIAS_FUNCTION(LoadTyped_RW, IMG_LoadTyped_RW);
    }

    namespace mix {
        ALIAS_FUNCTION(OpenAudio, Mix_OpenAudio);
        ALIAS_FUNCTION(GetError, Mix_GetError);
        ALIAS_FUNCTION(Quit, Mix_Quit);

        constexpr auto default_frequency = MIX_DEFAULT_FREQUENCY;
        constexpr auto default_format = MIX_DEFAULT_FORMAT;
        constexpr auto default_channels = MIX_DEFAULT_CHANNELS;
    }

    namespace vk {
        ALIAS_FUNCTION(GetInstanceExtensions, SDL_Vulkan_GetInstanceExtensions);
        ALIAS_FUNCTION(CreateSurface, SDL_Vulkan_CreateSurface);
    }
};

export namespace sdl {

    struct initializer {
        initializer() {
            SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS |
                SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC | SDL_INIT_JOYSTICK |
                SDL_INIT_AUDIO);
        }
        ~initializer() {
            SDL_Quit();
        }
    };

    struct window_deleter {
        void operator()(SDL_Window* ptr) const {
            SDL_DestroyWindow(ptr);
        }
    };
    using unique_window = std::unique_ptr<SDL_Window, window_deleter>;

    struct surface_deleter {
        void operator()(SDL_Surface* ptr) const {
            SDL_FreeSurface(ptr);
        }
    };
    using unique_surface = std::unique_ptr<SDL_Surface, surface_deleter>;

    struct surface_lock {
        SDL_Surface* surface;
        surface_lock(SDL_Surface* surface) : surface(surface) {
            SDL_LockSurface(surface);
        }
        ~surface_lock() {
            SDL_UnlockSurface(surface);
        }
        void* pixels() {
            return surface->pixels;
        }
    };

    struct rwops_deleter {
        void operator()(SDL_RWops* ptr) const {
            SDL_RWclose(ptr);
        }
    };
    using unique_rwops = std::unique_ptr<SDL_RWops, rwops_deleter>;
}
