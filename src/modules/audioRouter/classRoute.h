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

#ifndef _CLASS_ROUTE_H
#define _CLASS_ROUTE_H

#include <string>
#include <vector>

/*
 * Some of a display's stream classes going elsewhere than the rest, kept
 * apart from the bus so it can be tested.
 *
 * The router sends a display's whole range of virtual sinks (the stream
 * classes of module-palm-policy) to the output it chose. An override sends a
 * few of those classes to another sink instead, as long as the output is a
 * given one: with a USB headset carrying the audio, the ringtones and the
 * alerts go to a sink that plays on the loudspeaker and the headset
 * together, the way a wired headset rings, while everything else stays in
 * the headset. The classes are module-palm-policy's EVirtualSink numbers.
 */
namespace ClassRoute
{
    struct Override
    {
        //! The output it holds for; it does nothing for any other.
        std::string whileOutput;
        //! The sink the classes go to instead.
        std::string target;
        std::vector<int> classes;

        bool active() const { return !whileOutput.empty() && !target.empty() && !classes.empty(); }
        bool operator==(const Override &o) const
        {
            return whileOutput == o.whileOutput && target == o.target && classes == o.classes;
        }
    };

    struct Range
    {
        int start;
        int end;
        std::string sink;

        bool operator==(const Range &o) const
        {
            return start == o.start && end == o.end && sink == o.sink;
        }
    };

    /* Routing the classes start..end (inclusive) to output, as runs of
     * classes that go to one sink, in order: the whole range in one when
     * the override does not hold for this output, else split around the
     * classes it moves. A range that is empty (start > end) is passed on
     * as it is. */
    std::vector<Range> split(int start, int end, const std::string &output, const Override &override);
}

#endif // _CLASS_ROUTE_H
