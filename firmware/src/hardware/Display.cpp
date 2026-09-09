#include "Display.h"
#include "BoardConfig.h"

namespace {
Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(
    Board::LcdDc,
    Board::LcdCs,
    Board::LcdClock,
    Board::LcdMosi,
    GFX_NOT_DEFINED
);
}

// Keep construction and exported alias in the same translation unit so
// initialization order is deterministic before AppRuntime::begin().
Arduino_GFX *gfx = new Arduino_ST7789(
    lcdBus,
    Board::LcdReset,
    3,
    true,
    Board::DisplayWidth,
    Board::DisplayHeight,
    0,
    0,
    0,
    80
);

Arduino_GFX *Display::graphics()
{
    return gfx;
}
