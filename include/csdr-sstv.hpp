#pragma once

#include <csdr/module.hpp>
#include <vector>
#include "modes.hpp"

#define SAMPLERATE 12000.0

namespace Csdr::Sstv {

    enum DecoderState { SYNC, DATA };

    class Metrics {
        public:
            float error;
            float offset;
            int8_t invert;
            bool operator < (Metrics other) {
                return error < other.error;
            }
    };

    class StdDevResult {
        public:
            float average;
            float deviation;
    };

    struct OutputDescription {
        // lets reserve 2 bytes for extended vis codes
        uint16_t vis;
        uint16_t pixels;
        uint16_t lines;
    };

    char outputSync[4] = { 'S', 'Y', 'N', 'C' };

    class SstvDecoder: public Csdr::Module<float, unsigned char> {
        public:
            explicit SstvDecoder();
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
            // vis bit high
            const float carrier_1100 = 1100.0 / (SAMPLERATE / 2);
            // vis bit low
            const float carrier_1300 = 1300.0 / (SAMPLERATE / 2);

            DecoderState state = SYNC;
            std::vector<Metrics> previous_errors;
            Mode* mode = nullptr;
            float offset = 0.0;
            // possible values: 1 and -1, should not take other values.
            // 1 is regular (USB), -1 is inverted (LSB)
            int8_t invert = 1;

            uint16_t currentLine = 0;

            Metrics getSyncError(float* input);
            bool attemptVisDecode(const float* input);
            int getVis(const float* input);
            static StdDevResult calculateStandardDeviation(const float* input, size_t len);
            void lineSync(float duration, bool firstSync);

            void readColorLine();
            void convertLineData(unsigned char* raw);
            void convertYUVPixel(unsigned char* dst, uint8_t Y, int Cr, int Cb);

            unsigned char* yuvBackBuffer;
    };

}