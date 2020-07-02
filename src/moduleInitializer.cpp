// Copyright (c) 2020 LG Electronics, Inc.
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

#include "moduleInitializer.h"

ModuleInitializer::ModuleInitializer(const std::stringstream& audioModuleConfigPath):\
                                  mAudioModuleConfigPath(audioModuleConfigPath.str())
{
    g_debug("ModuleIntializer constructor");
    initializeModuleInfo();
}

ModuleInitializer::~ModuleInitializer()
{
    g_debug("ModuleInitializer destructor");
    for (const auto& it : mSetAudioModules)
    {
        if (it == "load_audio_policy_manager")
            unload_audio_policy_manager();
    }
}

void ModuleInitializer::initializeModuleInfo()
{
    g_debug("initializeModuleInfo");
    moduleMap.insert(std::pair<std::string, ModuleInitFunction>\
        ("load_audio_policy_manager", load_audio_policy_manager));
    //these should be enabled when the modules are implemented
    /*moduleMap.insert(std::pair<std::string, ModuleInitFunction>\
        ("load_luna_event_subscriber", load_luna_event_subscriber));
    moduleMap.insert(std::pair<std::string, ModuleInitFunction>\
        ("load_bluetooth_manager", load_bluetooth_manager));
    moduleMap.insert(std::pair<std::string, ModuleInitFunction>\
        ("load_avdtp_manager", load_avdtp_manager));
    moduleMap.insert(std::pair<std::string, ModuleInitFunction>\
        ("load_play_record_sound", load_play_record_sound));
    moduleMap.insert(std::pair<std::string, ModuleInitFunction>\
        ("load_master_volume_manager", load_master_volume_manager));
    moduleMap.insert(std::pair<std::string, ModuleInitFunction>\
        ("load_connection_manager", load_connection_manager));*/
}

bool ModuleInitializer::registerAudioModules()
{
    g_debug("loadAudioModules");
    JValue configJson = JDomParser::fromFile(mAudioModuleConfigPath.c_str(),JSchema::AllSchema());
    if (!configJson.isValid() || !configJson.isObject())
    {
        g_debug("failed to parse file using defaults. File: %s. Error: %s",\
            mAudioModuleConfigPath.c_str(), configJson.errorString().c_str());
        return false;
    }
    if (configJson.hasKey("load_module"))
    {
        g_debug("found load_module key");
        JValue moduleInfo = configJson["load_module"];
        if (!moduleInfo.isArray())
        {
            g_debug("moduleInfo is not an array");
            return false;
        }
        else
        {
            std::string strModule;
            mSetAudioModules.clear();
            for (auto &elements:moduleInfo.items())
            {
                strModule = elements.asString();
                mSetAudioModules.insert(strModule);
                std::map<std::string, ModuleInitFunction>::iterator it = moduleMap.find(strModule);
                if (it != moduleMap.end())
                {
                    g_debug("loadAudioModules: registering module:%s", strModule.c_str());
                    registerAudioModule(it->second);
                }
            }
            return true;
        }
    }
    else
    {
        g_debug("load_module key not found");
        return false;
    }
}