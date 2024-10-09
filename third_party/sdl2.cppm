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
    ALIAS_FUNCTION(GameControllerRumble, SDL_GameControllerRumble);
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

    namespace GameControllerButtonValues {
        constexpr GameControllerButton INVALID = SDL_CONTROLLER_BUTTON_INVALID;
        constexpr GameControllerButton A = SDL_CONTROLLER_BUTTON_A;
        constexpr GameControllerButton B = SDL_CONTROLLER_BUTTON_B;
        constexpr GameControllerButton X = SDL_CONTROLLER_BUTTON_X;
        constexpr GameControllerButton Y = SDL_CONTROLLER_BUTTON_Y;
        constexpr GameControllerButton BACK = SDL_CONTROLLER_BUTTON_BACK;
        constexpr GameControllerButton GUIDE = SDL_CONTROLLER_BUTTON_GUIDE;
        constexpr GameControllerButton START = SDL_CONTROLLER_BUTTON_START;
        constexpr GameControllerButton LEFTSTICK = SDL_CONTROLLER_BUTTON_LEFTSTICK;
        constexpr GameControllerButton RIGHTSTICK = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
        constexpr GameControllerButton LEFTSHOULDER = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        constexpr GameControllerButton RIGHTSHOULDER = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
        constexpr GameControllerButton DPAD_UP = SDL_CONTROLLER_BUTTON_DPAD_UP;
        constexpr GameControllerButton DPAD_DOWN = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        constexpr GameControllerButton DPAD_LEFT = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
        constexpr GameControllerButton DPAD_RIGHT = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        constexpr GameControllerButton MISC1 = SDL_CONTROLLER_BUTTON_MISC1;
        constexpr GameControllerButton PADDLE1 = SDL_CONTROLLER_BUTTON_PADDLE1;
        constexpr GameControllerButton PADDLE2 = SDL_CONTROLLER_BUTTON_PADDLE2;
        constexpr GameControllerButton PADDLE3 = SDL_CONTROLLER_BUTTON_PADDLE3;
        constexpr GameControllerButton PADDLE4 = SDL_CONTROLLER_BUTTON_PADDLE4;
        constexpr GameControllerButton TOUCHPAD = SDL_CONTROLLER_BUTTON_TOUCHPAD;
    };
    namespace GameControllerAxisValues {
        constexpr GameControllerAxis INVALID = SDL_CONTROLLER_AXIS_INVALID;
        constexpr GameControllerAxis LEFTX = SDL_CONTROLLER_AXIS_LEFTX;
        constexpr GameControllerAxis LEFTY = SDL_CONTROLLER_AXIS_LEFTY;
        constexpr GameControllerAxis RIGHTX = SDL_CONTROLLER_AXIS_RIGHTX;
        constexpr GameControllerAxis RIGHTY = SDL_CONTROLLER_AXIS_RIGHTY;
        constexpr GameControllerAxis TRIGGERLEFT = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
        constexpr GameControllerAxis TRIGGERRIGHT = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
    };

    constexpr auto enable = SDL_ENABLE;
    constexpr auto disable = SDL_DISABLE;

    namespace image {
        ALIAS_FUNCTION(Load, IMG_Load);
        ALIAS_FUNCTION(LoadTyped_RW, IMG_LoadTyped_RW);
    }

    namespace mix {
        // General
        ALIAS_FUNCTION(Linked_Version, Mix_Linked_Version);
        ALIAS_FUNCTION(Init, Mix_Init);
        ALIAS_FUNCTION(Quit, Mix_Quit);
        ALIAS_FUNCTION(OpenAudio, Mix_OpenAudio);
        ALIAS_FUNCTION(CloseAudio, Mix_CloseAudio);
        ALIAS_FUNCTION(SetError, Mix_SetError);
        ALIAS_FUNCTION(GetError, Mix_GetError);
        ALIAS_FUNCTION(QuerySpec, Mix_QuerySpec);

        // Samples
        ALIAS_FUNCTION(GetNumChunkDecoders, Mix_GetNumChunkDecoders);
        ALIAS_FUNCTION(GetChunkDecoder, Mix_GetChunkDecoder);
        ALIAS_FUNCTION(LoadWAV, Mix_LoadWAV);
        ALIAS_FUNCTION(LoadWAV_RW, Mix_LoadWAV_RW);
        ALIAS_FUNCTION(QuickLoad_WAV, Mix_QuickLoad_WAV);
        ALIAS_FUNCTION(QuickLoad_RAW, Mix_QuickLoad_RAW);
        ALIAS_FUNCTION(VolumeChunk, Mix_VolumeChunk);
        ALIAS_FUNCTION(FreeChunk, Mix_FreeChunk);

        // Channels
        ALIAS_FUNCTION(AllocateChannels, Mix_AllocateChannels);
        ALIAS_FUNCTION(Volume, Mix_Volume);
        ALIAS_FUNCTION(PlayChannel, Mix_PlayChannel);
        ALIAS_FUNCTION(PlayChannelTimed, Mix_PlayChannelTimed);
        ALIAS_FUNCTION(FadeInChannel, Mix_FadeInChannel);
        ALIAS_FUNCTION(FadeInChannelTimed, Mix_FadeInChannelTimed);
        ALIAS_FUNCTION(Pause, Mix_Pause);
        ALIAS_FUNCTION(Resume, Mix_Resume);
        ALIAS_FUNCTION(HaltChannel, Mix_HaltChannel);
        ALIAS_FUNCTION(ExpireChannel, Mix_ExpireChannel);
        ALIAS_FUNCTION(FadeOutChannel, Mix_FadeOutChannel);
        ALIAS_FUNCTION(ChannelFinished, Mix_ChannelFinished);
        ALIAS_FUNCTION(Playing, Mix_Playing);
        ALIAS_FUNCTION(Paused, Mix_Paused);
        ALIAS_FUNCTION(FadingChannel, Mix_FadingChannel);
        ALIAS_FUNCTION(GetChunk, Mix_GetChunk);

        // Groups
        ALIAS_FUNCTION(ReserveChannels, Mix_ReserveChannels);
        ALIAS_FUNCTION(GroupChannel, Mix_GroupChannel);
        ALIAS_FUNCTION(GroupChannels, Mix_GroupChannels);
        ALIAS_FUNCTION(GroupCount, Mix_GroupCount);
        ALIAS_FUNCTION(GroupAvailable, Mix_GroupAvailable);
        ALIAS_FUNCTION(GroupOldest, Mix_GroupOldest);
        ALIAS_FUNCTION(GroupNewer, Mix_GroupNewer);
        ALIAS_FUNCTION(FadeOutGroup, Mix_FadeOutGroup);
        ALIAS_FUNCTION(HaltGroup, Mix_HaltGroup);

        // Music
        ALIAS_FUNCTION(GetNumMusicDecoders, Mix_GetNumMusicDecoders);
        ALIAS_FUNCTION(GetMusicDecoder, Mix_GetMusicDecoder);
        ALIAS_FUNCTION(LoadMUS, Mix_LoadMUS);
        ALIAS_FUNCTION(FreeMusic, Mix_FreeMusic);
        ALIAS_FUNCTION(PlayMusic, Mix_PlayMusic);
        ALIAS_FUNCTION(FadeInMusic, Mix_FadeInMusic);
        ALIAS_FUNCTION(FadeInMusicPos, Mix_FadeInMusicPos);
        ALIAS_FUNCTION(HookMusic, Mix_HookMusic);
        ALIAS_FUNCTION(VolumeMusic, Mix_VolumeMusic);
        ALIAS_FUNCTION(PauseMusic, Mix_PauseMusic);
        ALIAS_FUNCTION(ResumeMusic, Mix_ResumeMusic);
        ALIAS_FUNCTION(RewindMusic, Mix_RewindMusic);
        ALIAS_FUNCTION(SetMusicPosition, Mix_SetMusicPosition);
        ALIAS_FUNCTION(SetMusicCMD, Mix_SetMusicCMD);
        ALIAS_FUNCTION(HaltMusic, Mix_HaltMusic);
        ALIAS_FUNCTION(FadeOutMusic, Mix_FadeOutMusic);
        ALIAS_FUNCTION(HookMusicFinished, Mix_HookMusicFinished);
        ALIAS_FUNCTION(GetMusicType, Mix_GetMusicType);
        ALIAS_FUNCTION(PlayingMusic, Mix_PlayingMusic);
        ALIAS_FUNCTION(PausedMusic, Mix_PausedMusic);
        ALIAS_FUNCTION(FadingMusic, Mix_FadingMusic);
        ALIAS_FUNCTION(GetMusicHookData, Mix_GetMusicHookData);

        // Effects
        ALIAS_FUNCTION(RegisterEffect, Mix_RegisterEffect);
        ALIAS_FUNCTION(UnregisterEffect, Mix_UnregisterEffect);
        ALIAS_FUNCTION(UnregisterAllEffects, Mix_UnregisterAllEffects);
        ALIAS_FUNCTION(SetPostMix, Mix_SetPostMix);
        ALIAS_FUNCTION(SetPanning, Mix_SetPanning);
        ALIAS_FUNCTION(SetDistance, Mix_SetDistance);
        ALIAS_FUNCTION(SetPosition, Mix_SetPosition);
        ALIAS_FUNCTION(SetReverseStereo, Mix_SetReverseStereo);


        constexpr auto default_frequency = MIX_DEFAULT_FREQUENCY;
        constexpr auto default_format = MIX_DEFAULT_FORMAT;
        constexpr auto default_channels = MIX_DEFAULT_CHANNELS;

        using Chunk = Mix_Chunk;
        using Music = Mix_Music;
        using MusicType = Mix_MusicType;
        using Fadeing = Mix_Fading;
        using EffectFunc = Mix_EffectFunc_t;
        using EffectDone = Mix_EffectDone_t;
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

    namespace mix {
        struct mix_chunk_deleter {
            void operator()(Mix_Chunk* ptr) const {
                Mix_FreeChunk(ptr);
            }
        };
        using unique_chunk = std::unique_ptr<Mix_Chunk, mix_chunk_deleter>;

        struct mix_music_deleter {
            void operator()(Mix_Music* ptr) const {
                Mix_FreeMusic(ptr);
            }
        };
        using unique_music = std::unique_ptr<Mix_Music, mix_music_deleter>;
    }
}
