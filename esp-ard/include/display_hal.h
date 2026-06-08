#pragma once

#include <esp_display_panel.hpp>

bool displayHalInit();
esp_panel::board::Board *displayHalBoard();
bool displayHalLvglLock(int timeoutMs = -1);
void displayHalLvglUnlock();
