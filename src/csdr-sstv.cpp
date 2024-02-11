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
            // assuming component 0 is the longest one
            float required = mode->getLineSyncDuration() + mode->getComponentCount() * (mode->getComponentSyncDuration(0) * 2 + mode->getComponentDuration(0));
            return reader->available() > (size_t) (required * SAMPLERATE);
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
                    if (it->error < .3) {
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

    unsigned char* pixels = writer->getWritePointer();

    for (uint8_t i = 0; i < mode->getComponentCount(); i++) {
        unsigned int lineSamples = (unsigned int) (mode->getComponentDuration(i) * SAMPLERATE);
        float samplesPerPixel = (float) lineSamples / mode->getHorizontalPixels();

        if (mode->getLineSyncPosition() == i) {
           std::cerr << "performing line sync on " << (int) i << "; line sync error: " << lineSync(carrier_1200, mode->getLineSyncDuration()) << std::endl;
            //reader->advance(.004862 * SAMPLERATE);
        }

        if (mode->hasComponentSync()) {
            if (i > 0) {
                lineSync(carrier_1200, mode->getComponentSyncDuration(i) * SAMPLERATE);
            }
        } else {
            reader->advance((size_t) (mode->getComponentSyncDuration(i) * SAMPLERATE));
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
    bool found = false;
    while (passedSamples++ < timeoutSamples) {
        float average = 0;
        for (unsigned int i = 0; i < 10; i++) {
            average += input[passedSamples + i];
        }
        //std::cerr << ">";
        float sample = (float) invert * (average / 10) - offset;
        error += fabsf(sample - carrier);
        if (sample > threshold) {
            found = true;
            break;
        }
    }
    std::cerr << std::endl;
    unsigned int toMove = passedSamples;
    if (!found) {
        toMove = (unsigned int) (duration * SAMPLERATE);
    }
    std::cerr << "found: " << found << "; moving by " << toMove << " samples; expected: " << duration * SAMPLERATE << std::endl;
    reader->advance(toMove);
    return error / (float) passedSamples;
}
