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
            virtual float getComponentSyncDuration() = 0;
            virtual float getComponentDuration() = 0;
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
            float getLineSyncDuration() override { return .007; }
            // this is all mostly wrong since we can't correctly model the YCrCb color model here yet
            unsigned int getComponentCount() override { return 2; }
            bool hasComponentSync() override { return true; }
            float getComponentSyncDuration() override { return .003; }
            float getComponentDuration() override { return .06 ;}
    };

    class WraaseSC1Mode: public Mode {
        public:
            explicit WraaseSC1Mode(int visCode);
            uint16_t getHorizontalPixels() override { return getHorizontalPixelsBit() ? 256 : 128; }
            uint16_t getVerticalLines() override { return getVerticalLinesBit() ? 256 : 128; }
            float getLineSyncDuration() override { return .006; }
            unsigned int getComponentCount() override { return 3; }
            bool hasComponentSync() override { return true; }
            float getComponentSyncDuration() override { return .006; }
            float getComponentDuration() override { return getHorizontalPixelsBit() ? .108 : .54; }
    };

    class MartinMode: public Mode {
        public:
            explicit MartinMode(int visCode);
            uint16_t getVerticalLines() override { return getVerticalLinesBit() ? 256 : 128; }
            float getLineSyncDuration() override { return .004862; }
            unsigned int getComponentCount() override { return 3; }
            bool hasComponentSync() override { return false; }
            float getComponentSyncDuration() override { return .000738; }
            float getComponentDuration() override { return getHorizontalPixelsBit() ? .146432 : .073216; }
    };

    class ScottieMode: public Mode {
        public:
            explicit ScottieMode(int visCode);
            uint16_t getVerticalLines() override { return getVerticalLinesBit() ? 256 : 128; }
            float getLineSyncDuration() override { return .009; }
            uint8_t getLineSyncPosition() override { return 2; }
            unsigned int getComponentCount() override { return 3; }
            bool hasComponentSync() override { return false; }
            float getComponentSyncDuration() override { return .0015; }
            float getComponentDuration() override { return getHorizontalPixelsBit() ? .138240 : .088064; }
    };

    class ScottieDXMode: public ScottieMode {
        public:
            explicit ScottieDXMode(): ScottieMode(76) {}
            float getComponentDuration() override { return .3456; }
    };

}