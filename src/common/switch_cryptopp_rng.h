// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#ifdef __SWITCH__

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <cryptopp/cryptlib.h>

extern "C" void randomGet(void* buf, std::size_t len);

namespace CryptoPP {

class AutoSeededRandomPool final : public RandomNumberGenerator {
public:
    std::string AlgorithmName() const override {
        return "SwitchRandom";
    }

    void GenerateBlock(byte* output, std::size_t size) override {
        randomGet(output, size);
    }

    void GenerateBlock(void* output, std::size_t size) {
        randomGet(output, size);
    }

    byte GenerateByte() override {
        std::uint8_t value{};
        GenerateBlock(&value, sizeof(value));
        return value;
    }

    word32 GenerateWord32(word32 min = 0, word32 max = 0xFFFFFFFF) override {
        std::uint32_t value{};
        randomGet(&value, sizeof(value));
        if (min == 0 && max == 0xFFFFFFFF) {
            return value;
        }
        if (max <= min) {
            return min;
        }
        return min + (value % (max - min + 1));
    }
};

} // namespace CryptoPP

#endif
