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

#ifndef _WAV_HEADER_H
#define _WAV_HEADER_H

#include <cstddef>
#include <cstdint>

/*
 * What a WAV file's header says about its samples, kept apart from the
 * playback thread so it can be tested.
 *
 * playSound wrote a .wav file to its stream byte for byte, header and all,
 * in the format the caller named: the header played as a click, and a file
 * in any other format than the one asked for played at the wrong speed or
 * as noise. The header says the format and where the samples are.
 */
namespace WavHeader
{
struct Format
{
    unsigned bitsPerSample; //!< 8, 16, 24 or 32, integer PCM
    unsigned channels;
    unsigned rate;          //!< samples per second
    std::size_t dataOffset; //!< where the samples start in the file
    std::size_t dataSize;   //!< how many bytes of samples follow
};

/*
 * Reads the RIFF/WAVE header at the start of `data` (`size` bytes of the
 * file's beginning). True, with `out` filled in, for integer PCM the header
 * fully describes within those bytes; false for anything else - not a WAV
 * file, compressed or float samples, a format chunk after the samples, or a
 * header longer than what was read - and the caller plays the file as
 * before.
 */
bool parse(const std::uint8_t *data, std::size_t size, Format &out);
}

#endif // _WAV_HEADER_H
