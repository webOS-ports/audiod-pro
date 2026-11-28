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

#include "classRoute.h"

#include <algorithm>

namespace ClassRoute
{

std::vector<Range> split(int start, int end, const std::string &output, const Override &override)
{
    if (!override.active() || output != override.whileOutput || start > end)
        return {Range{start, end, output}};

    std::vector<Range> runs;
    for (int c = start; c <= end; c++)
    {
        const bool moved = std::find(override.classes.begin(), override.classes.end(), c)
                           != override.classes.end();
        const std::string &sink = moved ? override.target : output;
        if (!runs.empty() && runs.back().sink == sink && runs.back().end == c - 1)
            runs.back().end = c;
        else
            runs.push_back(Range{c, c, sink});
    }
    return runs;
}

std::vector<Range> redirect(const std::vector<Range> &runs, const Redirect &redirect)
{
    if (!redirect.active())
        return runs;

    std::vector<Range> out;
    for (const Range &run : runs)
    {
        for (int c = run.start; c <= run.end; c++)
        {
            const bool moved = run.sink == redirect.from &&
                               std::find(redirect.classes.begin(), redirect.classes.end(), c)
                               != redirect.classes.end();
            const std::string &sink = moved ? redirect.to : run.sink;
            if (!out.empty() && out.back().sink == sink && out.back().end == c - 1)
                out.back().end = c;
            else
                out.push_back(Range{c, c, sink});
        }
        /* An empty range is passed on as split() passes it on. */
        if (run.start > run.end)
            out.push_back(run);
    }
    return out;
}

}
