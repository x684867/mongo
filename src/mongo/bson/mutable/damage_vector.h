/* Copyright 2013 10gen Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <vector>

#include "mongo/platform/cstdint.h"

namespace mongo {
namespace mutablebson {

    // A damage event represents a change of size 'size' byte at starting at offset
    // 'target_offset' in some target buffer, with the replacement data being 'size' bytes of
    // data from the 'source' offset. The base addresses against which these offsets are to be
    // applied are not captured here.
    struct DamageEvent {
        typedef uint32_t OffsetSizeType;

        // Offset of source data (in some buffer held elsewhere).
        OffsetSizeType sourceOffset;

        // Offset of target data (in some buffer held elsewhere).
        OffsetSizeType targetOffset;

        // Size of the damage region.
        size_t size;
    };

    typedef std::vector<DamageEvent> DamageVector;

} // namespace mutablebson
} // namespace mongo
