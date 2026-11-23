#pragma once
// Host-only mock button mapping. Does not read or authorize physical inputs.
#include "native_model.h"
namespace dm::native {
enum class MockButton { wheel_left,wheel_right,wheel_press,back,select,refresh,unknown };
inline bool dispatch_mock(MockButton button,Controller& controller) {
    switch(button) {
        case MockButton::wheel_left:controller.press(Key::up);return true;
        case MockButton::wheel_right:controller.press(Key::down);return true;
        case MockButton::wheel_press:controller.press(Key::enter);return true;
        case MockButton::back:controller.press(Key::back);return true;
        case MockButton::select:controller.press(Key::select);return true;
        case MockButton::refresh:controller.press(Key::refresh);return true;
        default:return false;
    }
}
} // namespace dm::native
