#pragma once

#include <cstdint>

#define SYSTEMCODE_ROBOT 0
#define SYSTEMCODE_WRAASE_SC1 1
#define SYSTEMCODE_MARTIN 2
#define SYSTEMCODE_SCOTTIE 3

namespace Csdr::Sstv {

    enum ColorMode { BW, RGB, GBR, YUV420, YUV422, YUV420PD };

    class Mode {
        public:
            virtual ~Mode() = default;
            static Mode* fromVis(int visCode);
            virtual uint16_t getHorizontalPixels() { return getHorizontalPixelsBit() ? 320 : 160;}
            virtual uint16_t getVerticalLines() { return getVerticalLinesBit() ? 240 : 120; }
            virtual bool hasLineSync() { return true; }
            virtual float getLineSyncDuration() = 0;
            virtual uint8_t getLineSyncPosition() { return 0; }
            virtual unsigned int getComponentCount() = 0;
            virtual bool hasComponentSync() = 0;
            // if component sync is false, this is used as an inter-component delay
            virtual float getComponentSyncDuration(uint8_t iteration) = 0;
            virtual float getComponentDuration(uint8_t iteration) = 0;
            // this transforms GBR -> RGB in the decoder.
            virtual ColorMode getColorMode() { return isColorMode() ? GBR : BW; }
            virtual uint8_t getLinesPerLineSync() { return 1; }
            // this is important for buffer allocation
            virtual float getLineDuration() {
                return
                    // double line sync duration for the worst case scanario
                    getLineSyncDuration() * 2 +
                    // this assumes that no component is shorter than the first one
                    getComponentCount() * (getComponentSyncDuration(0) + getComponentDuration(0));
            }
        protected:
            // protected constructor... use fromVis() or a derived class.
            explicit Mode(int visCode): visCode(visCode) {}
            int visCode;
            bool getHorizontalPixelsBit() const { return (visCode & 0b00000100) >> 2; }
            bool getVerticalLinesBit() const { return (visCode & 0b00001000) >> 3; }
            bool isColorMode() const { return (visCode & 0b00000011) == 0; }
    };

    class RobotMode: public Mode {
        public:
            explicit RobotMode(int visCode): Mode(visCode) {}
            float getLineSyncDuration() override {
                switch (visCode) {
                    // color 12
                    case 0:
                        return .007;
                    // color 24
                    case 4:
                        return .012;
                    // color 36
                    case 8:
                        return .009;
                    // color 72
                    case 12:
                        return .009;
                    // B&W 8
                    case 1:
                    case 2:
                    case 3:
                        return .01;
                    // B&W 12
                    case 5:
                    case 6:
                    case 7:
                        return .007;
                    // B&W 24
                    case 9:
                    case 10:
                    case 11:
                    // B&W 36
                    case 13:
                    case 14:
                    case 15:
                        return .12;
                }
                // switch should be exhaustive by VIS code
                return .007;
            }
            // this is all mostly wrong since we can't correctly model the YCrCb color model here yet
            unsigned int getComponentCount() override {
                if (!isColorMode()) return 1;
                switch (visCode) {
                    case 0:
                    case 8:
                        return 2;
                    case 4:
                    case 12:
                        return 3;
                }
                return 2;
            }
            bool hasComponentSync() override { return false; }
            float getComponentSyncDuration(uint8_t iteration) override {
                switch (visCode) {
                    case 0:
                        return iteration == 0 ? 0 : .003;
                    case 4:
                        return iteration == 0 ? 0 : .006;
                    case 8:
                        return iteration == 0 ? .003 : .006;
                    case 12:
                        return iteration == 0 ? .003 : .006;
                }
                return .003;
            }
            float getComponentDuration(uint8_t iteration) override {
                switch (visCode) {
                    case 0:
                        return iteration == 0 ? .06 : .03;
                    case 4:
                        return iteration == 0 ? .088 : .044;
                    case 8:
                        return iteration == 0 ? .088 : .044;
                    case 12:
                        return iteration == 0 ? .138 : .069;
                    // B&W 8
                    case 1:
                    case 2:
                    case 3:
                        return .056;
                    // B&W 12
                    case 5:
                    case 6:
                    case 7:
                    // B&W 24
                    case 9:
                    case 10:
                    case 11:
                        return .093;
                    // B&W 36
                    case 13:
                    case 14:
                    case 15:
                        return .138;
                }
                return .06;
            }
            ColorMode getColorMode() override {
                if (!isColorMode()) return BW;
                switch (visCode) {
                    case 0:
                    case 8:
                        return YUV420;
                    case 4:
                    case 12:
                        return YUV422;
                }
                return YUV422;
            }
    };

    class WraaseSC1Mode: public Mode {
        public:
            explicit WraaseSC1Mode(int visCode): Mode(visCode) {}
            uint16_t getHorizontalPixels() override { return getHorizontalPixelsBit() ? 256 : 128; }
            uint16_t getVerticalLines() override { return getVerticalLinesBit() ? 256 : 128; }
            float getLineSyncDuration() override { return .006; }
            unsigned int getComponentCount() override { return 3; }
            bool hasComponentSync() override { return true; }
            float getComponentSyncDuration(uint8_t iteration) override { return .006; }
            float getComponentDuration(uint8_t iteration) override { return getHorizontalPixelsBit() ? .108 : .54; }
    };

    class WraaseSC2Mode: public Mode {
        public:
            explicit WraaseSC2Mode(int visCode): Mode(visCode) {}
            uint16_t getHorizontalPixels() override { return 320; }
            uint16_t getVerticalLines() override { return visCode == 51 ? 128 : 256; }
            float getLineSyncDuration() override { return .005; }
            unsigned int getComponentCount() override { return 3; }
            bool hasComponentSync() override { return false; }
            float getComponentSyncDuration(uint8_t iteration) override { return .0005; }
            float getComponentDuration(uint8_t iteration) override {
                switch (visCode) {
                    case 51: // SC-2 30
                    case 59: // SC-2 60
                        return iteration == 1 ? .117 : .058;
                    case 63: // SC-2 120
                        return iteration == 1 ? .235 : .117;
                    case 55: // SC-2 180
                    default:
                        return .235;
                }
            }
            ColorMode getColorMode() override { return RGB; }
            float getLineDuration() override {
                return
                // double line sync duration for the worst case scanario
                getLineSyncDuration() * 2 +
                // for this mode, the longest component is the second one
                getComponentCount() * (getComponentSyncDuration(1) + getComponentDuration(1));
            }
    };

    class MartinMode: public Mode {
        public:
            explicit MartinMode(int visCode): Mode(visCode) {}
            uint16_t getVerticalLines() override { return getVerticalLinesBit() ? 256 : 128; }
            float getLineSyncDuration() override { return .004862; }
            unsigned int getComponentCount() override { return 3; }
            bool hasComponentSync() override { return false; }
            float getComponentSyncDuration(uint8_t iteration) override { return .000572; }
            float getComponentDuration(uint8_t iteration) override { return getHorizontalPixelsBit() ? .146432 : .073216; }
    };

    class ScottieMode: public Mode {
        public:
            explicit ScottieMode(int visCode): Mode(visCode) {}
            uint16_t getVerticalLines() override { return getVerticalLinesBit() ? 256 : 128; }
            float getLineSyncDuration() override { return .009; }
            uint8_t getLineSyncPosition() override { return 2; }
            unsigned int getComponentCount() override { return 3; }
            bool hasComponentSync() override { return false; }
            float getComponentSyncDuration(uint8_t iteration) override { return .0015; }
            float getComponentDuration(uint8_t iteration) override { return getHorizontalPixelsBit() ? .138240 : .088064; }
    };

    class ScottieDXMode: public ScottieMode {
        public:
            explicit ScottieDXMode(): ScottieMode(76) {}
            float getComponentDuration(uint8_t iteration) override { return .3456; }
    };

    class PDMode: public Mode {
        public:
            explicit PDMode(int visCode): Mode(visCode) {};
            uint16_t getVerticalLines() override {
                switch (visCode) {
                    case 93:
                    case 99:
                        return 256;
                    case 95:
                    case 96:
                    case 97:
                        return 496;
                    case 98:
                        return 400;
                    case 94:
                        return 616;
                }
                return Mode::getVerticalLines();
            }
            uint16_t getHorizontalPixels() override {
                switch (visCode) {
                    case 93:
                    case 99:
                        return 320;
                    case 95:
                    case 96:
                    case 97:
                        return 640;
                    case 98:
                        return 512;
                    case 94:
                        return 800;
                }
                return Mode::getHorizontalPixels();
            }
            float getLineSyncDuration() override { return .020; }
            unsigned int getComponentCount() override { return 4; }
            bool hasComponentSync() override { return false; }
            float getComponentSyncDuration(uint8_t iteration) override {
                return iteration == 0 ? .00208 : 0;
            }
            float getComponentDuration(uint8_t iteration) override {
                switch (visCode) {
                    case 93: // PD 50
                        return .09152;
                    case 99: // PD 90
                        return .170240;
                    case 95: // PD 120
                        return .1216;
                    case 98: // PD 160
                        return .195584;
                    case 96: // PD 180
                        return .18304;
                    case 97: // PD 240
                        return .24448;
                    case 94: // PD 290
                        return .2288;
                }
                return .09152;
            }
            ColorMode getColorMode() override { return YUV420PD; }
            uint8_t getLinesPerLineSync() override { return 2; }
    };

}