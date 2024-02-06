#pragma once

#include <csdr/module.hpp>

#define SAMPLERATE 12000.0

namespace Csdr::Sstv {

    enum DecoderState { SYNC, VIS, DATA };

    enum Mode { ROBOT, WRAASE, MARTIN, SCOTTIE, AVT };

    class SstvConfig {
        public:
            explicit SstvConfig(uint8_t vis);
            bool isColor();
            uint16_t getHorizontalPixels();
            uint16_t getVerticalLines();
        private:
            uint8_t colorMode;
            bool horizontalResolution;
            bool verticalResolution;
            Mode mode;
    };

    struct metrics {
        float error;
        float offset;
    };

    class SstvDecoder: public Csdr::Module<float, unsigned char> {
        public:
            ~SstvDecoder() override;
            bool canProcess() override;
            void process() override;
        private:
            // image sync
            const float carrier_1900 = 1900.0 / (SAMPLERATE / 2);
            // image sync and line sync
            const float carrier_1200 = 1200.0 / (SAMPLERATE / 2);
            // min color
            const float carrier_1500 = 1500.0 / (SAMPLERATE / 2);
            // max color
            const float carrier_2300 = 2300.0 / (SAMPLERATE / 2);

            DecoderState state = SYNC;
            float previous_error = INFINITY;
            SstvConfig* config = nullptr;
            float offset = 0.0;

            uint16_t currentLine = 0;

            float getSyncError(float* input);
            metrics calculateMetrics(float* input, size_t len, float target);
            int getVis();
            float readRawVis();
            float lineSync(float carrier, float duration);

            void readColorLine();
    };

}