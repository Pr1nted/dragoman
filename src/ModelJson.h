#ifndef DRAGOMAN_MODELJSON_H
#define DRAGOMAN_MODELJSON_H

#include "Formats.h"

namespace dragoman {

Json     worldToJson(const World& w);
bool     worldFromJson(const Json& j, World& w, Report& report);
uint64_t rasterDigest(const std::vector<uint32_t>& raster);

}  // namespace dragoman

#endif
