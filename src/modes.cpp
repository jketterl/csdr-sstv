#include "modes.hpp"

using namespace Csdr::Sstv;

Mode* Mode::fromVis(int visCode) {
    switch (visCode) {
        // Scottie DX overrides AVT
        case 76:
            return new ScottieDXMode();
    }
    int systemCode = (visCode & 0b01110000) >> 4;
    switch (systemCode) {
        case SYSTEMCODE_ROBOT:
            return new RobotMode(visCode);
        case SYSTEMCODE_WRAASE_SC1:
            return new WraaseSC1Mode(visCode);
        case SYSTEMCODE_MARTIN:
            return new MartinMode(visCode);
        case SYSTEMCODE_SCOTTIE:
            return new ScottieMode(visCode);
        //case SYSTEMCODE_AVT:
        //    return new AvtMode(visCode);
    }
    return nullptr;
}

Mode::Mode(int visCode): visCode(visCode) {};

bool Mode::getHorizontalPixelsBit() {
    return (visCode & 0b00000100) >> 2;
}

bool Mode::getVerticalLinesBit() {
    return (visCode & 0b00001000) >> 3;
}

uint16_t Mode::getHorizontalPixels() {
    return getHorizontalPixelsBit() ? 320 : 160;
}

uint16_t Mode::getVerticalLines() {
    return getVerticalLinesBit() ? 240 : 120;
}

RobotMode::RobotMode(int visCode): Mode(visCode) {};

WraaseSC1Mode::WraaseSC1Mode(int visCode): Mode(visCode) {};

MartinMode::MartinMode(int visCode): Mode(visCode) {};

ScottieMode::ScottieMode(int visCode): Mode(visCode) {};
