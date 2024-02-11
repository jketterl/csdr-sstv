#include "modes.hpp"

using namespace Csdr::Sstv;

Mode* Mode::fromVis(int visCode) {
    switch (visCode) {
        // Scottie DX overrides AVT
        case 76:
            return new ScottieDXMode();
        // these don't fit the pattern
        case 93:
        case 94:
        case 95:
        case 96:
        case 97:
        case 98:
        case 99:
            return new PDMode(visCode);
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
