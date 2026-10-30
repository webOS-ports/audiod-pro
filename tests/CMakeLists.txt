# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

# Unit tests for the parts of audiod that stand on their own: the
# Bluetooth manager's bookkeeping (btState) and the call state the legacy
# manager routes and pauses media by (callState), and whether something is
# ringing (ringState), and which output a volume request moves
# (volumeTarget).

find_package(GTest REQUIRED)

add_executable(audiod-tests
    ${CMAKE_SOURCE_DIR}/src/modules/bluetoothManager/btState.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/palmLegacyManager/callState.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/palmLegacyManager/ringState.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/masterVolumeManager/volumeTarget.cpp
    test_btstate.cpp
    test_callstate.cpp
    test_ringstate.cpp
    test_volumetarget.cpp)
target_include_directories(audiod-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/modules/bluetoothManager
    ${CMAKE_SOURCE_DIR}/src/modules/palmLegacyManager
    ${CMAKE_SOURCE_DIR}/src/modules/masterVolumeManager)
target_compile_options(audiod-tests PRIVATE -Wall -Wextra)
target_link_libraries(audiod-tests GTest::gtest GTest::gtest_main pthread)
add_test(NAME audiod-tests COMMAND audiod-tests)
