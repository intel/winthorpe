// Copyright (c) 2014 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPEECH_SPEECH_INSTANCE_H_
#define SPEECH_SPEECH_INSTANCE_H_

#include <glib.h>
#include <thread>  // NOLINT
#include <queue>
#include <string>

#include "common/extension.h"
#include "common/picojson.h"

class SpeechInstance : public common::Instance {
 public:
  SpeechInstance();
  virtual ~SpeechInstance();

 protected:
  static gboolean IOWatchCb(GIOChannel* source,
                            GIOCondition condition,
                            gpointer data);
  static gboolean ProcessPendingRequestsCb(gpointer data);
  static gboolean ProcessPendingRepliesCb(gpointer data);

 private:
  // common::Instance implementation.
  virtual void HandleMessage(const char* msg);
  virtual void HandleSyncMessage(const char* message);

  void QueueReply(const std::string& reply);
  void QueueRequest(const std::string& req);
  bool SendRequest(const char* message);
  uint32_t ReadReply(char** reply);
#ifdef TIZEN
  static void SetupMainloop(void *data);
  GMainLoop* main_loop_;
  std::thread  thread_;
#endif  // TIZEN

  std::queue<std::string> pending_replies_;
  std::queue<std::string> pending_requests_;
  int fd_;
  GIOChannel *channel_;
  guint watcher_id_;
  guint pending_request_timer_;
  guint pending_reply_timer_;
};
#endif  // SPEECH_SPEECH_INSTANCE_H_
