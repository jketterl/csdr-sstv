#include "csdr-sstv.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>

using namespace Csdr::Sstv;

SstvDecoder::~SstvDecoder() {
    delete mode;
}

bool SstvDecoder::canProcess() {
    switch (state) {
        case SYNC:
            // calibration header = 300ms + 10ms + 300ms
            // VIS code = 30ms * 10;
            // total 910 ms
            return reader->available() > (size_t) (.91 * SAMPLERATE);
        case DATA:
            // total time for one line
            // worst case
            // assuming component sync takes twice as long
            float required = mode->getLineSyncDuration() + mode->getComponentCount() * (mode->getComponentSyncDuration() * 2 + mode->getComponentDuration());
            return reader->available() > (size_t) (required * SAMPLERATE);
    }
    return false;
}

void SstvDecoder::process() {
    float* input = reader->getReadPointer();
    switch (state) {
        case SYNC: {
            Metrics m = getSyncError(input);
            if (m.error < 0.2) {
                // wait until we have reached the point of least error
                previous_errors.push_back(m);
                std::cerr << "error cache size: " << previous_errors.size() << "; error: " << m.error << std::endl;
                if (previous_errors.size() > 100) {
                    auto it = std::min_element(previous_errors.begin(), previous_errors.end());
                    if (it == previous_errors.begin() && it->error < .1) {
                        std::cerr << "overflow; sync error: " << it->error << "; offset: " << it->offset << "; invert: " << (int) it->invert << std::endl;
                        offset = it->offset;
                        invert = it->invert;
                        if (attemptVisDecode(input + 7220)) {
                            unsigned int syncOffset = 0;
                            if (mode->getLineSyncPosition() == 0) {
                                syncOffset = mode->getLineSyncDuration() * SAMPLERATE;
                            }
                            reader->advance(7220 + 3600 - syncOffset);
                            break;
                        }
                    }
                    previous_errors.erase(previous_errors.begin());
                }
                reader->advance(1);
            } else {
                if (!previous_errors.empty()) {
                    auto it = std::min_element(previous_errors.begin(), previous_errors.end());
                    if (it->error < .1) {
                        auto age = std::distance(it, previous_errors.end());
                        std::cerr << "age: " << age << " sync error: " << it->error << "; offset: " << it->offset << "; invert: " << (int) it->invert << std::endl;
                        offset = it->offset;
                        invert = it->invert;
                        if (attemptVisDecode(input + 7320 - age)) {
                            unsigned int syncOffset = 0;
                            if (mode->getLineSyncPosition() == 0) {
                                syncOffset = mode->getLineSyncDuration() * SAMPLERATE;
                            }
                            reader->advance((7320 - age) + 3600 - syncOffset);
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
            if (++currentLine >= mode->getVerticalLines()) {
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
    state = DATA;
    return true;
}

Metrics SstvDecoder::getSyncError(float *input) {

    Metrics m[3] = {
        calculateMetrics(input, 3600, carrier_1900),
        calculateMetrics(input + 3600, 120, carrier_1200),
        calculateMetrics(input + 3720, 3600, carrier_1900),
    };

    float min_offset = INFINITY, max_offset = 0;
    float error = 0.0;
    float offset_sum = 0.0;
    for (unsigned int i = 0; i < 3; i++) {
        min_offset = std::min(min_offset, m[i].offset);
        max_offset = std::max(max_offset, m[i].offset);
        offset_sum += m[i].offset;
        error += m[i].error;
    }

    // gotta be within 100 Hz
    float max_deviation = 100.0 / (SAMPLERATE / 2);
    if (max_offset - min_offset < max_deviation) {
        return {
            .error = error / 3,
            .offset = offset_sum / 3,
            .invert = 1,
        };
    }

    // try the same again in inverse mode
    Metrics n[3] = {
        calculateMetrics(input, 3600, -carrier_1900),
        calculateMetrics(input + 3600, 120, -carrier_1200),
        calculateMetrics(input + 3720, 3600, -carrier_1900),
    };
    min_offset = 0, max_offset = -INFINITY;
    error = 0.0;
    offset_sum = 0.0;
    for (unsigned int i = 0; i < 3; i++) {
        min_offset = std::min(min_offset, n[i].offset);
        max_offset = std::max(max_offset, n[i].offset);
        offset_sum += n[i].offset;
        error += n[i].error;
    }
    if (max_offset - min_offset < max_deviation) {
        return {
            .error = error / 3,
            .offset = offset_sum / 3,
            .invert = -1,
        };
    }

    return {
        .error = INFINITY
    };
}

Metrics SstvDecoder::calculateMetrics(float *input, size_t len, float target) {
    float sum = 0.0;
    //unsigned int counter = 0;
    for (ssize_t i = 10; i < len - 10; i += 10) {
        float d = input[i] - target;
        sum += d;
        //counter++;
    }
    float offset = sum / (float) ((len - 20) / 10);
    //std::cerr << "counter: " << counter << "; calculator: " << ((len - 20) / 10) << std::endl;

    float error = 0.0;
    for (ssize_t i = 10; i < len - 10; i++) {
        error += fabsf(input[i] - offset - target);
    }

    return Metrics {
        .error = error / (float) (len - 20),
        .offset = offset,
    };
}

int SstvDecoder::getVis(const float* input) {
    uint8_t result = 0;
    bool parity = false;
    float visError = 0.0;
    visError += fabsf(readRawVis(input) - carrier_1200);
    unsigned int numSamples = .03 * SAMPLERATE;
    for (unsigned int i = 0; i < 7; i++) {
        float raw = readRawVis(input + (i + 1) * numSamples);
        bool visBit = raw < carrier_1200;
        if (visBit) {
            visError += fabsf(raw - carrier_1100);
        } else {
            visError += fabsf(raw - carrier_1300);
        }
        result |= visBit << i;
        parity ^= visBit;
    }
    bool parityBit = readRawVis(input + 8 * numSamples) < carrier_1200;
    if (parity != parityBit) {
        std::cerr << "vis parity check failed" << std::endl;
        return -1;
    }
    visError += fabsf(readRawVis(input + 9 * numSamples) - carrier_1200);
    if (visError > .1) {
        std::cerr << "bad overall VIS error: " << visError << std::endl;
        return -1;
    }
    std::cerr << "overall VIS error: " << visError << std::endl;
    return result;
}

float SstvDecoder::readRawVis(const float* input) {
    unsigned int numSamples = .03 * SAMPLERATE;
    float average = 0.0;
    for (unsigned int k = 10; k < numSamples - 10; k++) {
        average += input[k];
    }
    return ((float) invert * average) / (float) (numSamples - 20) - offset;
}

void SstvDecoder::readColorLine() {
    if (writer->writeable() < mode->getHorizontalPixels() * 3) {
        std::cerr << "could not write image data";
        return;
    }

    unsigned char* pixels = writer->getWritePointer();
    unsigned int lineSamples = (unsigned int) (mode->getComponentDuration() * SAMPLERATE);
    float samplesPerPixel = (float) lineSamples / mode->getHorizontalPixels();

    for (unsigned int i = 0; i < mode->getComponentCount(); i++) {
        if (mode->getLineSyncPosition() == i) {
           std::cerr << "performing line sync on " << i << "; line sync error: " << lineSync(carrier_1200, mode->getLineSyncDuration()) << std::endl;
            //reader->advance(.004862 * SAMPLERATE);
        }

        if (mode->hasComponentSync()) {
            if (i > 0) {
                lineSync(carrier_1200, mode->getComponentSyncDuration() * SAMPLERATE);
            }
        } else {
            reader->advance(mode->getComponentSyncDuration() * SAMPLERATE);
        }
        float* input = reader->getReadPointer();
        // TODO complex color systems (YCrCb)
        unsigned int color = (i + mode->getColorRotation()) % 3;
        for (unsigned int k = 0; k < mode->getHorizontalPixels(); k++) {
            float raw = 0.0;
            for (unsigned int l = 0; l < (unsigned int) samplesPerPixel; l++) {
                raw += input[(unsigned int) (k * samplesPerPixel) + l];
            }
            raw = ((float) invert * raw - offset) / (unsigned int) samplesPerPixel;
            if (raw < carrier_1500) {
                pixels[k * 3 + color] = 0;
            } else if (raw > carrier_2300) {
                pixels[k * 3 + color] = 255;
            } else {
                pixels[k * 3 + color] = (uint8_t) (((raw - carrier_1500) / (carrier_2300 - carrier_1500)) * 255);
            }
        }
        reader->advance(lineSamples);
    }
    writer->advance(mode->getHorizontalPixels() * 3);
}

float SstvDecoder::lineSync(float carrier, float duration) {
    // allow uncertainty of 10%
    unsigned int timeoutSamples = (unsigned int) (duration * SAMPLERATE * 1.1);
    float* input = reader->getReadPointer();
    float error = 0.0;
    // within 100 Hz of carrier
    float threshold = carrier + 100.0 / (SAMPLERATE / 2);
    unsigned int passedSamples = 0;
    std::cerr << "line sync: threshold = " << threshold;
    /*
    while (passedSamples++ < timeoutSamples) {
        std::cerr << "<";
        float sample = (float) invert * input[passedSamples] - offset;
        error += sample - carrier;
        if (sample < threshold) break;
    }
    */
    passedSamples = (unsigned int) (duration * SAMPLERATE * 0.9);
    while (passedSamples++ < timeoutSamples) {
        float average = 0;
        for (unsigned int i = 0; i < 10; i++) {
            average += input[passedSamples + i];
        }
        std::cerr << ">";
        float sample = (float) invert * (average / 10) - offset;
        error += fabsf(sample - carrier);
        if (sample > threshold) break;
    }
    std::cerr << std::endl;
    reader->advance(std::min(passedSamples + 5, timeoutSamples));
    return error / (float) passedSamples;
}
