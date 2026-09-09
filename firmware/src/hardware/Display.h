#pragma once
#include <Arduino_GFX_Library.h>

// Exported display pointer is constructed in Display.cpp. Keeping the
// storage and construction in one translation unit avoids cross-TU
// static-initialization ordering hazards.
extern Arduino_GFX *gfx;

namespace Display {
Arduino_GFX *graphics();
}
