// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "wavHeader.h"

#include <cstring>

namespace
{
    const unsigned kFormatPcm = 1;
    const unsigned kFormatExtensible = 0xFFFE;

    std::uint32_t le16(const std::uint8_t *p)
    {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8);
    }

    std::uint32_t le32(const std::uint8_t *p)
    {
        return le16(p) | (le16(p + 2) << 16);
    }
}

bool WavHeader::parse(const std::uint8_t *data, std::size_t size, Format &out)
{
    if (!data || size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        return false;

    bool haveFormat = false;
    Format format{};
    std::size_t pos = 12;

    // Chunks follow one another, each an id, a length and the body, padded
    // to an even length.
    while (size - pos >= 8)
    {
        const std::uint8_t *chunk = data + pos;
        const std::size_t length = le32(chunk + 4);
        const std::size_t body = pos + 8;

        if (std::memcmp(chunk, "fmt ", 4) == 0)
        {
            if (length < 16 || size - body < 16)
                return false;
            const unsigned tag = le16(chunk + 8);
            // WAVE_FORMAT_EXTENSIBLE carries integer PCM too; its sub-format
            // GUID starts with the PCM tag.
            if (tag == kFormatExtensible)
            {
                if (length < 40 || size - body < 40 || le16(chunk + 8 + 24) != kFormatPcm)
                    return false;
            }
            else if (tag != kFormatPcm)
                return false;
            format.channels = le16(chunk + 10);
            format.rate = le32(chunk + 12);
            format.bitsPerSample = le16(chunk + 22);
            haveFormat = true;
        }
        else if (std::memcmp(chunk, "data", 4) == 0)
        {
            // The samples end the header; a format after them is too late.
            format.dataOffset = body;
            format.dataSize = length;
            break;
        }

        if (length > size - body)
            return false;
        pos = body + length + (length & 1);
    }

    if (!haveFormat || format.dataOffset == 0)
        return false;
    if (format.bitsPerSample != 8 && format.bitsPerSample != 16 &&
        format.bitsPerSample != 24 && format.bitsPerSample != 32)
        return false;
    if (format.channels < 1 || format.channels > 6)
        return false;
    if (format.rate < 8000 || format.rate > 192000)
        return false;

    out = format;
    return true;
}
