#include "csdr-sstv.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>

using namespace Csdr::Sstv;

SstvDecoder::SstvDecoder(): Csdr::Module<float, unsigned char>() {
    yuvBackBuffer = (unsigned char*) malloc(320 * 2);
}

SstvDecoder::~SstvDecoder() {
    delete mode;
    delete yuvBackBuffer;
}

bool SstvDecoder::canProcess() {
    switch (state) {
        case SYNC:
            // calibration header = 300ms + 10ms + 300ms
            // VIS code = 30ms * 10;
            // total 910 ms
            return reader->available() > (size_t) (.91 * SAMPLERATE);
        case DATA:
            return reader->available() > (size_t) (mode->getLineDuration() * SAMPLERATE);
    }
    return false;
}

void SstvDecoder::process() {
    float* input = reader->getReadPointer();
    switch (state) {
        case SYNC: {
            Metrics m = getSyncError(input);
            if (m.error < 0.5) {
                // wait until we have reached the point of least error
                previous_errors.push_back(m);
                std::cerr << "error cache size: " << previous_errors.size() << "; error: " << m.error << std::endl;
                if (previous_errors.size() > 100) {
                    auto it = std::min_element(previous_errors.begin(), previous_errors.end());
                    if (it == previous_errors.begin() && it->error < .3) {
                        std::cerr << "overflow; sync error: " << it->error << "; offset: " << it->offset << "; invert: " << (int) it->invert << std::endl;
                        offset = it->offset;
                        invert = it->invert;
                        if (attemptVisDecode(input + 7220)) {
                            reader->advance(7220 + 3600 );
                            break;
                        }
                    }
                    previous_errors.erase(previous_errors.begin());
                }
                reader->advance(1);
            } else {
                if (!previous_errors.empty()) {
                    auto it = std::min_element(previous_errors.begin(), previous_errors.end());
                    if (it->error < .3) {
                        auto age = std::distance(it, previous_errors.end());
                        std::cerr << "age: " << age << " sync error: " << it->error << "; offset: " << it->offset << "; invert: " << (int) it->invert << std::endl;
                        offset = it->offset;
                        invert = it->invert;
                        if (attemptVisDecode(input + 7320 - age)) {
                            reader->advance((7320 - age) + 3600);
                            break;
                        }
                    }
                }
                previous_errors.clear();
                // advance quicker if we're not even below threshold
                reader->advance(10);
            }
            break;
        }
        case DATA: {
            std::cerr << "Reading line " << currentLine << std::endl;
            readColorLine();
            currentLine += mode->getLinesPerLineSync();
            if (currentLine >= mode->getVerticalLines()) {
                currentLine = 0;
                delete mode;
                mode = nullptr;
                state = SYNC;
            }
            break;
        }
    }
}

bool SstvDecoder::attemptVisDecode(const float *input) {
    int vis = getVis(input);
    if (vis < 0) return false;

    std::cerr << "Detected VIS: " << vis << std::endl;
    mode = Mode::fromVis(vis);
    if (mode == nullptr) {
        std::cerr << "mode not implemented; no mode for vis " << vis << std::endl;
        return false;
    }

    memcpy(writer->getWritePointer(), outputSync, sizeof(outputSync));
    writer->advance(sizeof(outputSync));
    OutputDescription out = {
        .vis = (uint16_t) vis,
        .pixels = mode->getHorizontalPixels(),
        .lines = mode->getVerticalLines(),
    };
    memcpy(writer->getWritePointer(), &out, sizeof(OutputDescription));
    writer->advance(sizeof(OutputDescription));

    previous_errors.clear();
    lineOffset = 0.0;
    state = DATA;
    return true;
}

Metrics SstvDecoder::getSyncError(float *input) {

    StdDevResult m[3] = {
        calculateStandardDeviation(input, 3600),
        calculateStandardDeviation(input + 3600, 120),
        calculateStandardDeviation(input + 3720, 3600),
    };

    float targets[3] = {
        carrier_1900,
        carrier_1200,
        carrier_1900,
    };

    // gotta be within 100 Hz
    float max_deviation = 100.0 / (SAMPLERATE / 2);

    // try for positive and negative (i.e. USB and LSB)
    for (int8_t factor : { 1, -1 }) {
        float min_offset = INFINITY, max_offset = 0;
        float error = 0.0;
        float offset_sum = 0.0;
        for (unsigned int i = 0; i < 3; i++) {
            offset = m[i].average - targets[i] * (float) factor;
            min_offset = std::min(min_offset, offset);
            max_offset = std::max(max_offset, offset);
            offset_sum += offset;
            error += m[i].deviation;
        }

        if (max_offset - min_offset < max_deviation) {
            return {
                .error = error / 3,
                .offset = offset_sum / 3,
                .invert = factor,
            };
        }
    }

    // deterrent
    return {
        .error = INFINITY
    };
}

int SstvDecoder::getVis(const float* input) {
    uint8_t result = 0;
    bool parity = false;
    unsigned int numSamples = .03 * SAMPLERATE;

    float visError = 0.0;
    StdDevResult results[10];
    for (unsigned int i = 0; i < 10; i++) {
        results[i] = calculateStandardDeviation(input + i * numSamples, numSamples);
        visError += results[i].deviation;
    }
    visError /= 10;

    if (visError > .1) {
        std::cerr << "bad overall VIS error: " << visError << std::endl;
        return -1;
    }

    for (unsigned int i = 0; i < 7; i++) {
        bool visBit = (float) invert * results[i + 1].average - offset < carrier_1200;
        result |= visBit << i;
        parity ^= visBit;
    }
    bool parityBit = (float) invert * results[8].average - offset < carrier_1200;
    if (parity != parityBit) {
        std::cerr << "vis parity check failed (would be vis = " << (int) result << ")" << std::endl;
        return -1;
    }
    std::cerr << "overall VIS error: " << visError << std::endl;
    return result;
}

StdDevResult SstvDecoder::calculateStandardDeviation(const float *input, size_t len) {
    float average = 0.0;
    for (unsigned int k = 0; k < len; k++) {
        average += input[k];
    }
    average /= (float) len;
    float sum = 0.0;
    for (unsigned int k = 0; k < len; k++) {
        sum += powf(input[k] - average, 2);
    }

    return StdDevResult{
        .average = average,
        .deviation = sqrtf(sum / (float) (len - 1)),
    };
}

void SstvDecoder::readColorLine() {
    if (writer->writeable() < mode->getHorizontalPixels() * 3) {
        std::cerr << "could not write image data";
        return;
    }

    unsigned char pixels[mode->getHorizontalPixels()][mode->getComponentCount()];

    for (unsigned int i = 0; i < mode->getComponentCount(); i++) {
        float lineSamples = mode->getComponentDuration(i) * SAMPLERATE;
        float samplesPerPixel = lineSamples / mode->getHorizontalPixels();

        if (mode->getLineSyncPosition() == i || (currentLine == 0 && i == 0)) {
           lineSync(mode->getLineSyncDuration(), currentLine == 0 && i == 0);
        }

        if (mode->hasComponentSync()) {
            if (i > 0) {
                lineSync(carrier_1200, mode->getComponentSyncDuration(i) * SAMPLERATE);
            }
        } else {
            reader->advance((size_t) (mode->getComponentSyncDuration(i) * SAMPLERATE));
        }
        float* input = reader->getReadPointer();
        for (unsigned int k = 0; k < mode->getHorizontalPixels(); k++) {
            float raw = 0.0;
            for (unsigned int l = 0; l < (unsigned int) samplesPerPixel; l++) {
                raw += input[(unsigned int) (k * samplesPerPixel) + l];
            }
            raw = (float) invert * (raw / (unsigned int) samplesPerPixel) - offset;
            if (raw < carrier_1500) {
                pixels[k][i] = 0;
            } else if (raw > carrier_2300) {
                pixels[k][i] = 255;
            } else {
                pixels[k][i] = (uint8_t) (((raw - carrier_1500) / (carrier_2300 - carrier_1500)) * 255);
            }
        }
        // try to get better timing precision by keeping a sub-sample floating point offset
        float to_advance = lineSamples + lineOffset;
        reader->advance((size_t) to_advance);
        float integral;
        lineOffset = modff(to_advance, &integral);
    }
    convertLineData((unsigned char*) pixels);
}

void SstvDecoder::convertLineData(unsigned char* raw) {
    unsigned char* dst = writer->getWritePointer();
    switch (mode->getColorMode()) {
        case BW:
            for (unsigned int i = 0; i < mode->getHorizontalPixels(); i++ ) {
                dst[i * 3] = dst[i * 3 + 1] = dst[i * 3 + 2] = raw[i];
            }
            writer->advance(mode->getHorizontalPixels() * 3);
            break;
        case RGB:
            std::memcpy(dst, raw, mode->getHorizontalPixels() * 3);
            writer->advance(mode->getHorizontalPixels() * 3);
            break;
        case GBR:
            for (unsigned int i = 0; i < mode->getHorizontalPixels(); i++) {
                // GBR -> RGB color mapping
                dst[i * 3] = raw[i * 3 + 2];
                dst[i * 3 + 1] = raw[i * 3];
                dst[i * 3 + 2] = raw[i * 3 + 1];
            }
            writer->advance(mode->getHorizontalPixels() * 3);
            break;
        case YUV422:
            for (unsigned int i = 0; i < mode->getHorizontalPixels(); i++) {
                convertYUVPixel(dst + i * 3, raw[i * 3], raw[i * 3 + 1] - 128, raw[i * 3 + 2] - 128);
            }
            writer->advance(mode->getHorizontalPixels() * 3);
            break;
        case YUV420:
            if (currentLine % 2) {
                for (unsigned int i = 0; i < mode->getHorizontalPixels(); i++) {
                    convertYUVPixel(dst + i * 3, yuvBackBuffer[i * 2], yuvBackBuffer[i * 2 + 1] - 128, raw[i * 2 + 1] - 128);
                }
                writer->advance(mode->getHorizontalPixels() * 3);
                dst = writer->getWritePointer();
                for (unsigned int i = 0; i < mode->getHorizontalPixels(); i++) {
                    convertYUVPixel(dst + i * 3, raw[i * 2], yuvBackBuffer[i * 2 + 1] - 128, raw[i * 2 + 1] - 128);
                }
                writer->advance(mode->getHorizontalPixels() * 3);
            } else {
                std::memcpy(yuvBackBuffer, raw, mode->getHorizontalPixels() * 2);
            }
            break;
        case YUV420PD:
            for (unsigned int i = 0; i < mode->getHorizontalPixels(); i++) {
                convertYUVPixel(dst + i * 3, raw[i * 4], raw[i * 4 + 1] - 128, raw[i * 4 + 2] - 128);
            }
            writer->advance(mode->getHorizontalPixels() * 3);
            dst = writer->getWritePointer();
            for (unsigned int i = 0; i < mode->getHorizontalPixels(); i++) {
                convertYUVPixel(dst + i * 3, raw[i * 4 + 3], raw[i * 4 + 1] - 128, raw[i * 4 + 2] - 128);
            }
            writer->advance(mode->getHorizontalPixels() * 3);
            break;
    }
}

void SstvDecoder::convertYUVPixel(unsigned char *dst, uint8_t Y, int Cr, int Cb) {
    dst[0] = std::min(255, std::max(0, Y + 45 * Cr / 32));
    dst[1] = std::min(255, std::max(0, Y - (11 * Cb + 23 * Cr) / 32));
    dst[2] = std::min(255, std::max(0, Y + 113 * Cb / 64));
}

void SstvDecoder::lineSync(float duration, bool firstSync) {
    // allow uncertainty of 10%
    unsigned int timeoutSamples = (unsigned int) (duration * SAMPLERATE * 1.5);
    float* input = reader->getReadPointer();
    // within 100 Hz of carrier
    float threshold = carrier_1200 + 100.0 / (SAMPLERATE / 2);
    unsigned int passedSamples = 0;
    if (!firstSync) {
        passedSamples = (unsigned int) (duration * SAMPLERATE * 0.9);
    }
    bool found = false;
    unsigned int count = 0;
    unsigned int to_average = 50;
    while (passedSamples < timeoutSamples) {
        count = 0;
        for (unsigned int i = 0; i < to_average; i++) {
            float sample = (float) invert * input[passedSamples + i] - offset;
            if (sample > threshold) count++;
        }
        if (count > to_average / 2) {
            found = true;
            break;
        }
        passedSamples++;
    }
    unsigned int toMove = passedSamples + (to_average - count);
    if (!found) {
        toMove = (unsigned int) (duration * SAMPLERATE);
    }
    std::cerr << "found: " << found << "; moving by " << toMove << " samples; expected: " << duration * SAMPLERATE << std::endl;
    reader->advance(toMove);
}
