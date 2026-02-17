#pragma once
#include <vector>
#include <cstdint>

inline constexpr uint32_t REGISTER_SCAN_INTERVAL_MS = 30000;

class Register {
    public:
        int Mode;
        int ConversionID;
        int offset;
        int registryID;
        int dataSize;
        int dataType;
        const char* label;
        char asString[30];

    Register(int Mode_, int ConversionID_, int offset_, int registryID_,
             int dataSize_, int dataType_, const char* label_)
        : Mode(Mode_), ConversionID(ConversionID_), offset(offset_),
          registryID(registryID_), dataSize(dataSize_),
          dataType(dataType_), label(label_) {
        asString[0] = '\0';
    }
};


