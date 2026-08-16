#include "model/model_metadata.h"

namespace syj::edgemind {

std::string model_ftype_name(uint32_t file_type) {
    // Values transcribed directly from llama.cpp's public `enum
    // llama_ftype` (see model_metadata.h's header comment for source/date).
    // GUESSED (1024) is llama.cpp's own value for "not specified in the
    // model file" — included here so that specific, real case is labeled
    // accurately rather than falling into the generic "Unknown" branch.
    switch (file_type) {
        case 0:  return "F32";
        case 1:  return "F16";
        case 2:  return "Q4_0";
        case 3:  return "Q4_1";
        case 7:  return "Q8_0";
        case 8:  return "Q5_0";
        case 9:  return "Q5_1";
        case 10: return "Q2_K";
        case 11: return "Q3_K_S";
        case 12: return "Q3_K_M";
        case 13: return "Q3_K_L";
        case 14: return "Q4_K_S";
        case 15: return "Q4_K_M";
        case 16: return "Q5_K_S";
        case 17: return "Q5_K_M";
        case 18: return "Q6_K";
        case 19: return "IQ2_XXS";
        case 20: return "IQ2_XS";
        case 21: return "Q2_K_S";
        case 22: return "IQ3_XS";
        case 23: return "IQ3_XXS";
        case 24: return "IQ1_S";
        case 25: return "IQ4_NL";
        case 26: return "IQ3_S";
        case 27: return "IQ3_M";
        case 28: return "IQ2_S";
        case 29: return "IQ2_M";
        case 30: return "IQ4_XS";
        case 31: return "IQ1_M";
        case 32: return "BF16";
        case 36: return "TQ1_0";
        case 37: return "TQ2_0";
        case 38: return "MXFP4_MOE";
        case 39: return "NVFP4";
        case 40: return "Q1_0";
        case 1024: return "GUESSED (not specified in file)";
        default:
            return "Unknown (file_type=" + std::to_string(file_type) + ")";
    }
}

} // namespace syj::edgemind
