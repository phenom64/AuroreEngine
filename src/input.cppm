module;

#include <cstdint>

export module dreamrender:input;

import sdl2;

export namespace input {
    class keyboard_handler {
        public:
            virtual void key_down(sdl::Keysym key) {};
            virtual void key_up(sdl::Keysym key) {};
    };

    class controller_handler {
        public:
            virtual void add_controller(sdl::GameController* controller) {};
            virtual void remove_controller(sdl::GameController* controller) {};

            virtual void button_down(sdl::GameController* controller, sdl::GameControllerButton button) {};
            virtual void button_up(sdl::GameController* controller, sdl::GameControllerButton button) {};
            virtual void axis_motion(sdl::GameController* controller, sdl::GameControllerAxis axis, int16_t value) {};
    };
}
