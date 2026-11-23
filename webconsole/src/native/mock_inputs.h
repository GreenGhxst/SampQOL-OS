#pragma once
// Deliberate host-test mapping; never reads physical devices.
#include "native_model.h"
#include <string>
namespace dm::native {
enum class MockButton { wheel_left,wheel_right,wheel_press,back,select,refresh,unknown };
class MockInputs {
public:
    bool dispatch(MockButton button,Controller& controller) const {
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
};
} // namespace dm::native
