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

#ifndef _PULSE_MESSAGE_H
#define _PULSE_MESSAGE_H

#include <cstring>
#include <vector>

#include <pulse/module-palm-policy.h>

/*
 * One message from audiod to module-palm-policy, as it goes on the socket.
 *
 * The module reads every message as SIZE_MESG_TO_PULSE bytes: the header,
 * the message's own struct, and nothing it looks at after that. audiod sent
 * those 150 bytes from a buffer holding only the header and the struct (95
 * bytes for a paDeviceSet), so each message read past the end of its heap
 * block and handed PulseAudio whatever followed it. The message is now the
 * full SIZE_MESG_TO_PULSE bytes, zero after the struct.
 */
template <typename T>
std::vector<char> packPulseMessage(const paudiodMsgHdr &header, const T &body)
{
    static_assert(sizeof(paudiodMsgHdr) + sizeof(T) <= SIZE_MESG_TO_PULSE,
                  "a message to module-palm-policy is SIZE_MESG_TO_PULSE bytes at most");

    std::vector<char> message(SIZE_MESG_TO_PULSE, 0);
    std::memcpy(message.data(), &header, sizeof(paudiodMsgHdr));
    std::memcpy(message.data() + sizeof(paudiodMsgHdr), &body, sizeof(T));
    return message;
}

#endif // _PULSE_MESSAGE_H
