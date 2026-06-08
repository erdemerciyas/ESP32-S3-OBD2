#pragma once

#include <lvgl.h>

class SplashUi {
public:
    void create();
    void show();
    void hide();
    bool isVisible() const { return visible_; }

private:
    lv_obj_t *screen_ = nullptr;
    lv_obj_t *waitArc_ = nullptr;
    bool visible_ = false;
};
