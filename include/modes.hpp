#pragma once

#include <cstdint>

#define SYSTEMCODE_ROBOT 0
#define SYSTEMCODE_WRAASE_SC1 1
#define SYSTEMCODE_MARTIN 2
#define SYSTEMCODE_SCOTTIE 3
#define SYSTEMCODE_AVT 4

namespace Csdr::Sstv {

    class Mode {
        public:
            virtual ~Mode() = default;
            static Mode* fromVis(int visCode);
            virtual uint16_t getHorizontalPixels();
            virtual uint16_t getVerticalLines();
            virtual bool hasLineSync() { return true; }
            virtual float getLineSyncDuration() = 0;
            virtual uint8_t getLineSyncPosition() { return 0; }
            virtual unsigned int getComponentCount() = 0;
            virtual bool hasComponentSync() = 0;
            // if component sync is false, this is used as an inter-component delay
            virtual float getComponentSyncDuration(uint8_t iteration) = 0;
            virtual float getComponentDuration(uint8_t iteration) = 0;
            // this transforms GBR -> RGB in the decoder.
            virtual uint8_t getColorRotation() { return 1; }
        protected:
            // protected constructor... use fromVis() or a derived class.
            explicit Mode(int visCode);
            int visCode;
            bool getHorizontalPixelsBit();
            bool getVerticalLinesBit();
    };

    class RobotMode: public Mode {
        public:
            explicit RobotMode(int visCode);
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
                        return .0105;
                    // color 72
                    case 12:
                        return .009;
                }
                // switch should be exhaustive by VIS code
                return .007;
            }
            // this is all mostly wrong since we can't correctly model the YCrCb color model here yet
            unsigned int getComponentCount() override {
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
                        return iteration == 0 ? 0 : .0045;
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
                        return iteration == 0 ? .09 : .045;
                    case 12:
                        return iteration == 0 ? .138 : .069;
                }
                return .06;
            }
            uint8_t getColorRotation() override { return 0; }
    };

    class WraaseSC1Mode: public Mode {
        public:
            explicit WraaseSC1Mode(int visCode);
            uint16_t getHorizontalPixels() override { return getHorizontalPixelsBit() ? 256 : 128; }
            uint16_t getVerticalLines() override { return getVerticalLinesBit() ? 256 : 128; }
            float getLineSyncDuration() override { return .006; }
            unsigned int getComponentCount() override { return 3; }
            bool hasComponentSync() override { return true; }
            float getComponentSyncDuration(uint8_t iteration) override { return .006; }
            float getComponentDuration(uint8_t iteration) override { return getHorizontalPixelsBit() ? .108 : .54; }
    };

    class MartinMode: public Mode {
        public:
            explicit MartinMode(int visCode);
            uint16_t getVerticalLines() override { return getVerticalLinesBit() ? 256 : 128; }
            float getLineSyncDuration() override { return .0052786; }
            unsigned int getComponentCount() override { return 3; }
            bool hasComponentSync() override { return false; }
            float getComponentSyncDuration(uint8_t iteration) override { return .000738; }
            float getComponentDuration(uint8_t iteration) override { return getHorizontalPixelsBit() ? .146432 : .073216; }
    };

    class ScottieMode: public Mode {
        public:
            explicit ScottieMode(int visCode);
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

}