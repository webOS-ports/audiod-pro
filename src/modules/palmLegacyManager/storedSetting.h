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

#ifndef _STORED_SETTING_H
#define _STORED_SETTING_H

/*
 * A setting audiod keeps in settingsservice: it starts at its default, takes
 * the stored value once that has been read, and a change the user makes is
 * what counts from then on - also over a stored value that arrives after it
 * (settingsservice is asked when it comes up, which may be after the user
 * has already been to Settings).
 */
template <typename T>
class StoredSetting
{
public:
    explicit StoredSetting(T defaultValue) : mValue(defaultValue) {}

    const T &value() const { return mValue; }

    //! The stored value was read. True if value() changed.
    bool stored(const T &v)
    {
        if (mUserSet || v == mValue)
            return false;
        mValue = v;
        return true;
    }

    //! The user set it; it is to be stored. True if value() changed.
    bool set(const T &v)
    {
        mUserSet = true;
        if (v == mValue)
            return false;
        mValue = v;
        return true;
    }

private:
    T mValue;
    bool mUserSet = false;
};

#endif
