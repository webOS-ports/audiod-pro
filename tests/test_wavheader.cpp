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

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "wavHeader.h"

namespace
{
using Bytes = std::vector<std::uint8_t>;

void put16(Bytes &b, unsigned v)
{
    b.push_back(v & 0xff);
    b.push_back((v >> 8) & 0xff);
}

void put32(Bytes &b, std::uint32_t v)
{
    put16(b, v & 0xffff);
    put16(b, v >> 16);
}

void putId(Bytes &b, const char *id)
{
    b.insert(b.end(), id, id + 4);
}

Bytes fmtChunk(unsigned tag, unsigned channels, unsigned rate, unsigned bits)
{
    Bytes b;
    putId(b, "fmt ");
    put32(b, 16);
    put16(b, tag);
    put16(b, channels);
    put32(b, rate);
    put32(b, rate * channels * bits / 8);
    put16(b, channels * bits / 8);
    put16(b, bits);
    return b;
}

Bytes chunk(const char *id, const std::string &body)
{
    Bytes b;
    putId(b, id);
    put32(b, body.size());
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() & 1)
        b.push_back(0);
    return b;
}

Bytes wav(std::initializer_list<Bytes> chunks)
{
    Bytes b;
    putId(b, "RIFF");
    put32(b, 0); // not looked at
    putId(b, "WAVE");
    for (const Bytes &c : chunks)
        b.insert(b.end(), c.begin(), c.end());
    return b;
}

bool parse(const Bytes &b, WavHeader::Format &f)
{
    return WavHeader::parse(b.data(), b.size(), f);
}
}

// /usr/palm/sounds/notification.wav and alert.wav: 16-bit stereo, 44.1 kHz.
TEST(WavHeader, ThePlainHeaderOfTheSystemTones)
{
    const Bytes b = wav({fmtChunk(1, 2, 44100, 16), chunk("data", "abcd")});
    WavHeader::Format f{};
    ASSERT_TRUE(parse(b, f));
    EXPECT_EQ(f.bitsPerSample, 16u);
    EXPECT_EQ(f.channels, 2u);
    EXPECT_EQ(f.rate, 44100u);
    EXPECT_EQ(f.dataOffset, 44u);
    EXPECT_EQ(f.dataSize, 4u);
}

// Tools write LIST/INFO chunks, an odd-length one padded, before the samples.
TEST(WavHeader, ChunksBeforeTheSamplesAreSkipped)
{
    const Bytes b = wav({chunk("LIST", "INFOabc"), fmtChunk(1, 1, 22050, 8),
                         chunk("fact", "1234"), chunk("data", "xy")});
    WavHeader::Format f{};
    ASSERT_TRUE(parse(b, f));
    EXPECT_EQ(f.bitsPerSample, 8u);
    EXPECT_EQ(f.channels, 1u);
    EXPECT_EQ(f.rate, 22050u);
    // 12 + LIST (8 + 7 + pad) + fmt (24) + fact (12) + the data chunk's own 8.
    EXPECT_EQ(f.dataOffset, 12u + 16u + 24u + 12u + 8u);
    EXPECT_EQ(b[f.dataOffset], 'x');
    EXPECT_EQ(f.dataSize, 2u);
}

TEST(WavHeader, TheSamplesMayRunPastWhatWasRead)
{
    Bytes b = wav({fmtChunk(1, 2, 48000, 24)});
    putId(b, "data");
    put32(b, 1000000);
    WavHeader::Format f{};
    ASSERT_TRUE(parse(b, f));
    EXPECT_EQ(f.dataOffset, b.size());
    EXPECT_EQ(f.dataSize, 1000000u);
    EXPECT_EQ(f.bitsPerSample, 24u);
}

TEST(WavHeader, ExtensiblePcmIsTaken)
{
    Bytes fmt;
    putId(fmt, "fmt ");
    put32(fmt, 40);
    put16(fmt, 0xFFFE);
    put16(fmt, 2);
    put32(fmt, 48000);
    put32(fmt, 48000 * 2 * 4);
    put16(fmt, 8);
    put16(fmt, 32);
    put16(fmt, 22);   // cbSize
    put16(fmt, 32);   // valid bits
    put32(fmt, 3);    // channel mask
    put16(fmt, 1);    // sub-format: PCM ...
    fmt.insert(fmt.end(), 14, 0); // ... and the rest of its GUID
    const Bytes b = wav({fmt, chunk("data", "zz")});
    WavHeader::Format f{};
    ASSERT_TRUE(parse(b, f));
    EXPECT_EQ(f.bitsPerSample, 32u);
    EXPECT_EQ(f.rate, 48000u);

    // The same with a float sub-format is not taken.
    Bytes floatFmt = fmt;
    floatFmt[8 + 24] = 3;
    EXPECT_FALSE(parse(wav({floatFmt, chunk("data", "zz")}), f));
}

TEST(WavHeader, NotPcmIsNotTaken)
{
    WavHeader::Format f{};
    // IEEE float, and A-law.
    EXPECT_FALSE(parse(wav({fmtChunk(3, 2, 44100, 32), chunk("data", "ab")}), f));
    EXPECT_FALSE(parse(wav({fmtChunk(6, 1, 8000, 8), chunk("data", "ab")}), f));
}

TEST(WavHeader, FormatsPulseAudioIsNotAskedForAreNotTaken)
{
    WavHeader::Format f{};
    EXPECT_FALSE(parse(wav({fmtChunk(1, 2, 44100, 12), chunk("data", "ab")}), f));
    EXPECT_FALSE(parse(wav({fmtChunk(1, 0, 44100, 16), chunk("data", "ab")}), f));
    EXPECT_FALSE(parse(wav({fmtChunk(1, 7, 44100, 16), chunk("data", "ab")}), f));
    EXPECT_FALSE(parse(wav({fmtChunk(1, 2, 4000, 16), chunk("data", "ab")}), f));
    EXPECT_FALSE(parse(wav({fmtChunk(1, 2, 384000, 16), chunk("data", "ab")}), f));
    // The edges are fine.
    EXPECT_TRUE(parse(wav({fmtChunk(1, 6, 192000, 16), chunk("data", "ab")}), f));
    EXPECT_TRUE(parse(wav({fmtChunk(1, 1, 8000, 16), chunk("data", "ab")}), f));
}

TEST(WavHeader, NotAWavFile)
{
    WavHeader::Format f{};
    const std::string raw = "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10";
    EXPECT_FALSE(WavHeader::parse(reinterpret_cast<const std::uint8_t *>(raw.data()), raw.size(), f));
    Bytes riffNotWave = wav({fmtChunk(1, 2, 44100, 16), chunk("data", "ab")});
    riffNotWave[8] = 'A';
    EXPECT_FALSE(parse(riffNotWave, f));
    EXPECT_FALSE(WavHeader::parse(nullptr, 100, f));
    const Bytes tooShort(8, 0);
    EXPECT_FALSE(parse(tooShort, f));
}

TEST(WavHeader, AFormatAfterTheSamplesOrNoneAtAllIsNotTaken)
{
    WavHeader::Format f{};
    EXPECT_FALSE(parse(wav({chunk("data", "ab"), fmtChunk(1, 2, 44100, 16)}), f));
    EXPECT_FALSE(parse(wav({fmtChunk(1, 2, 44100, 16)}), f));
    EXPECT_FALSE(parse(wav({chunk("LIST", "abcd")}), f));
}

// What was read ends inside the header: a truncated format chunk, or a
// chunk claiming to be longer than what is there, before the samples.
TEST(WavHeader, AHeaderCutShortIsNotTaken)
{
    WavHeader::Format f{};
    Bytes cut = wav({fmtChunk(1, 2, 44100, 16)});
    cut.resize(cut.size() - 4);
    EXPECT_FALSE(parse(cut, f));

    // A format chunk shorter than the PCM fields, even when the bytes after
    // it would read as a sample size.
    Bytes shortFmt;
    putId(shortFmt, "fmt ");
    put32(shortFmt, 12);
    put16(shortFmt, 1);
    put16(shortFmt, 2);
    put32(shortFmt, 44100);
    put32(shortFmt, 44100 * 4);
    const char sixteen[5] = {'x', 'x', 16, 0, 0};
    const Bytes next = chunk(sixteen, "");
    shortFmt.insert(shortFmt.end(), next.begin(), next.end());
    EXPECT_FALSE(parse(wav({shortFmt, chunk("data", "abcdefgh")}), f));

    Bytes longList = wav({});
    putId(longList, "LIST");
    put32(longList, 100000);
    longList.insert(longList.end(), 20, 0);
    EXPECT_FALSE(parse(longList, f));
}

// The parse leaves `out` alone when it fails.
TEST(WavHeader, AFailedParseLeavesTheFormatAlone)
{
    WavHeader::Format f{16, 2, 44100, 44, 4};
    EXPECT_FALSE(parse(wav({fmtChunk(3, 1, 8000, 32), chunk("data", "ab")}), f));
    EXPECT_EQ(f.rate, 44100u);
    EXPECT_EQ(f.dataOffset, 44u);
}
