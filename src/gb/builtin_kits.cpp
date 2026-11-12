#include "gb/builtin_kits.h"

namespace gb {

namespace {
#include "gb/builtin_kits.inc"
}  // namespace

const uint8_t* builtinKit(int index) {
    return index == kBuiltinKit1 ? kBuiltinKitData0 : index == kBuiltinKit2 ? kBuiltinKitData1 : nullptr;
}

}  // namespace gb
