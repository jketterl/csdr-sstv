#include "csdr-sstv.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>

using namespace Csdr::Sstv;

SstvDecoder::~SstvDecoder() {
    delete config;
}

bool SstvDecoder::canProcess() {
    return reader->available() > 7321;
}

void SstvDecoder::process() {
    float* input = reader->getReadPointer();
    switch (state) {
        case SYNC: {
            metrics m = getSyncError(input);
            if (m.error < 0.2) {
                // wait until we have reached the point of least error
                previous_errors.push_back(m);
                if (previous_errors.size() > 100) {
                    auto it = std::min_element(previous_errors.begin(), previous_errors.end());
                    if (it == previous_errors.begin() && it->error < .1) {
                        std::cerr << "overflow; sync error: " << it->error << "; offset: " << it->offset << "; invert: " << (int) it->invert << std::endl;
                        offset = it->offset;
                        invert = it->invert;
                        reader->advance(7220);
                        previous_errors.clear();
                        state = VIS;
                        break;
                    }
                    previous_errors.erase(previous_errors.begin());
                }
                reader->advance(1);
            } else {
                if (!previous_errors.empty()) {
                    auto it = std::min_element(previous_errors.begin(), previous_errors.end());
                    if (it->error < .1) {
                        auto index = std::distance(previous_errors.begin(), it);
                        std::cerr << "sync error: " << it->error << "; offset: " << it->offset << "; invert: " << (int) it->invert << std::endl;
                        offset = it->offset;
                        invert = it->invert;
                        reader->advance(7220 + index);
                        previous_errors.clear();
                        state = VIS;
                        break;
                    }
                }
                previous_errors.clear();
                // advance quicker if we're not even below threshold
                reader->advance(10);
            }
            break;
        }
        case VIS: {
            int vis = getVis();
            if (vis < 0) {
                state = SYNC;
            } else {
                std::cerr << "Detected VIS: " << vis << std::endl;
                delete config;
                config = new SstvConfig(vis);
                state=DATA;
            }
            break;
        }
        case DATA: {
            std::cerr << "Reading line " << currentLine << std::endl;
            readColorLine();
            if (++currentLine >= config->getVerticalLines()) {
                currentLine = 0;
                state = SYNC;
            }
            break;
        }
    }
}

metrics SstvDecoder::getSyncError(float *input) {

    metrics m[3] = {
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
    metrics n[3] = {
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

metrics SstvDecoder::calculateMetrics(float *input, size_t len, float target) {
    float sum = 0.0;
    for (ssize_t i = 0; i < len; i += 10) {
        float d = input[i] - target;
        sum += d;
    }
    float offset = sum / (float) (len / 10);

    float error = 0.0;
    for (ssize_t i = 0; i < len; i++) {
        error += fabsf(input[i] - offset - target);
    }

    return metrics {
        .error = error / (float) len,
        .offset = offset,
    };
}

int SstvDecoder::getVis() {
    uint8_t result = 0;
    bool parity = false;
    float visError = 0.0;
    visError += readRawVis() - carrier_1200;
    for (unsigned int i = 0; i < 7; i++) {
        float raw = readRawVis();
        bool visBit = raw < carrier_1200;
        if (visBit) {
            visError += raw - carrier_1100;
        } else {
            visError += raw - carrier_1300;
        }
        result |= visBit << i;
        parity ^= visBit;
    }
    bool parityBit = readRawVis() < carrier_1200;
    if (parity != parityBit) {
        std::cerr << "vis parity check failed" << std::endl;
        return -1;
    }
    visError += readRawVis() - carrier_1200;
    if (visError > .1) {
        std::cerr << "bad overall VIS error: " << visError << std::endl;
        return -1;
    }
    return result;
}

float SstvDecoder::readRawVis() {
    unsigned int numSamples = .03 * SAMPLERATE;
    float average = 0.0;
    float* input = reader->getReadPointer();
    for (unsigned int k = 0; k < numSamples; k++) {
        average += input[k];
    }
    reader->advance(numSamples);
    return ((float) invert * average) / (float) numSamples - offset;
}

void SstvDecoder::readColorLine() {
    std::cerr << "line sync error: " << lineSync(carrier_1200, .004862) << std::endl;
    //reader->advance(.004862 * SAMPLERATE);

    uint8_t pixels[config->getHorizontalPixels()][3];
    unsigned int lineSamples = (unsigned int) (.146875 * SAMPLERATE);
    for (unsigned int i = 0; i < 3; i++) {
        if (i > 0) {
            reader->advance(.000572 * SAMPLERATE);
            //lineSync(carrier_1500, .000572);
        }
        float* input = reader->getReadPointer();
        // simple mapping for GBR -> RGB
        unsigned int color = (i + 1) % 3;
        for (unsigned int k = 0; k < config->getHorizontalPixels(); k++) {
            // todo average
            float raw = ((float) invert * input[k * lineSamples / config->getHorizontalPixels()]) - offset;
            if (raw < carrier_1500) {
                pixels[k][color] = 0;
            } else if (raw > carrier_2300) {
                pixels[k][color] = 255;
            } else {
                pixels[k][color] = (uint8_t) (((raw - carrier_1500) / (carrier_2300 - carrier_1500)) * 255);
            }
        }
        reader->advance(lineSamples);
    }
    std::memcpy(writer->getWritePointer(), pixels, config->getHorizontalPixels() * 3);
    writer->advance(config->getHorizontalPixels() * 3);
}

float SstvDecoder::lineSync(float carrier, float duration) {
    // allow uncertainty of 10%
    unsigned int timeoutSamples = (unsigned int) (duration * SAMPLERATE * 1.1);
    float* input = reader->getReadPointer();
    float error = 0.0;
    // within 100 Hz of carrier
    float threshold = carrier + 100.0 / (SAMPLERATE / 2);
    unsigned int passedSamples = 0;
    while (passedSamples++ < timeoutSamples) {
        float sample = (float) invert * input[passedSamples] - offset;
        error += sample - carrier;
        if (sample < threshold) break;
    }
    while (passedSamples++ < timeoutSamples) {
        float sample = (float) invert * input[passedSamples] - offset;
        error += input[passedSamples] - carrier;
        if (sample > threshold) break;
    }
    reader->advance(passedSamples);
    return error / (float) passedSamples;
}

SstvConfig::SstvConfig(uint8_t vis) {
    colorMode = vis & 0b11;
    horizontalResolution = (vis & 0b100) >> 2;
    verticalResolution = (vis & 0b1000) >> 3;
    mode = static_cast<Mode>((vis & 0b1110000) >> 4);
}

bool SstvConfig::isColor() {
    return colorMode == 0;
}

uint16_t SstvConfig::getHorizontalPixels() {
    return verticalResolution ? 320 : 160;
}

uint16_t SstvConfig::getVerticalLines() {
    return horizontalResolution ? 256 : 128;
}