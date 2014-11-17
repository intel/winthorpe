// Copyright (c) 2014 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "speech_extension.h"

#include "speech_logs.h"
#include "speech_instance.h"

std::ofstream _s_f_log;

common::Extension* CreateExtension() {
  return new SpeechExtension();
}

// This will be generated from speech_api.js
extern const char kSource_speech_api[];

SpeechExtension::SpeechExtension() {
  SetExtensionName("tizen.speechSynthesis");
  SetJavaScriptAPI(kSource_speech_api);
  const char *entry_pointer[] = {
    "tizen.SpeechRecognition",
    "tizen.SpeechSynthesisUtterance",
    NULL
  };
  SetExtraJSEntryPoints(entry_pointer);
}

SpeechExtension::~SpeechExtension() {}

common::Instance* SpeechExtension::CreateInstance() {
  LOG_INIT();
  return new SpeechInstance;
}
