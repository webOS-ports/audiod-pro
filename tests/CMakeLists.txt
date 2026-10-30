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
# ringing (ringState), when to reconnect to PulseAudio (paReconnect),
# when a video call goes on the loudspeaker (videoSpeaker),
# which output a volume request moves (volumeTarget), when an alert lowers
# the music (duckPolicy), what a WAV file's header says about the sound
# playSound plays (wavHeader), stopping on SIGTERM within a deadline
# (shutdownGuard), which output the audio goes to (outputChoice), what is
# sent to PulseAudio again after it restarted: the cards (cardTable) and
# the outputs' volumes and mutes (pulseReplay), and what a message to
# module-palm-policy holds (pulseMessage), whether a call belongs on a
# hands-free headset (btCall), oFono's hands-free audio agent against a
# private bus (hfpAgent), and stream classes the router sends elsewhere
# than the rest while an output is in use (classRoute).

find_package(GTest REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TEST_GIO REQUIRED gio-2.0 gio-unix-2.0)

add_executable(audiod-tests
    ${CMAKE_SOURCE_DIR}/src/modules/bluetoothManager/btState.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/palmLegacyManager/callState.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/palmLegacyManager/ringState.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/palmLegacyManager/paReconnect.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/palmLegacyManager/videoSpeaker.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/masterVolumeManager/volumeTarget.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/masterVolumeManager/pulseReplay.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/audioPolicyManager/duckPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/wavHeader.cpp
    ${CMAKE_SOURCE_DIR}/src/shutdownGuard.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/audioRouter/outputChoice.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/palmLegacyManager/btCall.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/palmLegacyManager/hfpAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/audioRouter/classRoute.cpp
    test_btstate.cpp
    test_callstate.cpp
    test_ringstate.cpp
    test_pareconnect.cpp
    test_videospeaker.cpp
    test_volumetarget.cpp
    test_duckpolicy.cpp
    test_wavheader.cpp
    test_shutdownguard.cpp
    test_cardtable.cpp
    test_pulsereplay.cpp
    test_pulsemessage.cpp
    test_outputchoice.cpp
    test_btcall.cpp
    test_hfpagent.cpp
    test_classroute.cpp)
target_include_directories(audiod-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/modules/bluetoothManager
    ${CMAKE_SOURCE_DIR}/src/modules/palmLegacyManager
    ${CMAKE_SOURCE_DIR}/src/modules/masterVolumeManager
    ${CMAKE_SOURCE_DIR}/src/modules/audioPolicyManager
    ${CMAKE_SOURCE_DIR}/src/modules/deviceManager
    ${CMAKE_SOURCE_DIR}/src/modules/audioRouter
    ${CMAKE_SOURCE_DIR}/include
    ${TEST_GIO_INCLUDE_DIRS})
target_compile_options(audiod-tests PRIVATE -Wall -Wextra)
target_link_libraries(audiod-tests GTest::gtest GTest::gtest_main ${TEST_GIO_LDFLAGS} pthread)
add_test(NAME audiod-tests COMMAND audiod-tests)
