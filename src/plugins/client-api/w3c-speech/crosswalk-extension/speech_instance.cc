// Copyright (c) 2014 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "speech_instance.h"

#include <gio/gio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <string>

#include "speech_logs.h"

#define WINTHORP_SERVER_SOCKET "@winthorpe.w3c-speech"
#ifdef TIZEN
// static
void SpeechInstance::SetupMainloop(void* data) {
  SpeechInstance* self = reinterpret_cast<SpeechInstance*>(data);

  g_main_loop_run(self->main_loop_);
}
#endif  // TIZEN

SpeechInstance::SpeechInstance()
#ifdef TIZEN
    : main_loop_(g_main_loop_new(0, FALSE))
    , thread_(SpeechInstance::SetupMainloop, this)
    , fd_(-1)
#else
    : fd_(-1)
#endif  // TIZEN
    , channel_(NULL)
    , watcher_id_(0)
    , pending_request_timer_(0)
    , pending_reply_timer_(0) {
#ifdef TIZEN
    thread_.detach();
#endif  // TIZEN
  if ((fd_ = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    ERR("Failed to create socket: %s", strerror(errno));
    fd_ = -1;
    return;
  }

  struct sockaddr_un server;
  memset(&server, 0, sizeof(server));
  server.sun_family = AF_UNIX,
  strncpy(server.sun_path, WINTHORP_SERVER_SOCKET, sizeof(server.sun_path) - 1);

  int len = SUN_LEN(&server);
  DBG("Socket path : %s", server.sun_path + 1);
  server.sun_path[0] = 0;

  if (connect(fd_, (struct sockaddr *)&server, len)) {
    ERR("Failed to connect to server : %s", strerror(errno));
    close(fd_);
    fd_ = -1;
    return;
  }

  channel_ = g_io_channel_unix_new(fd_);
  GIOCondition flags = GIOCondition(G_IO_IN | G_IO_ERR | G_IO_HUP);
  watcher_id_ = g_io_add_watch(channel_, flags, IOWatchCb, this);

  DBG("Connected to server");
}

SpeechInstance::~SpeechInstance() {
  if (watcher_id_)
    g_source_remove(watcher_id_);
  if (pending_reply_timer_)
    g_source_remove(pending_reply_timer_);
  if (pending_request_timer_)
    g_source_remove(pending_request_timer_);
  if (fd_)
    close(fd_);
  if (channel_)
    g_io_channel_unref(channel_);

#ifdef TIZEN
  g_main_loop_quit(main_loop_);
  g_main_loop_unref(main_loop_);
#endif  // TIZEN
}

// static
gboolean SpeechInstance::IOWatchCb(GIOChannel *c,
                                   GIOCondition cond,
                                   gpointer userdata) {
  SpeechInstance *self = reinterpret_cast<SpeechInstance*>(userdata);

  (void)c;

  DBG("IO Event on socket : %d", cond);

  switch (cond) {
    case G_IO_HUP:
    case G_IO_ERR:
      // TODO(avalluri): raise error and close the connection
      break;
    case G_IO_IN: {
      char *reply = NULL;
      uint32_t size = 0;

      if ((size = self->ReadReply(&reply))) {
        self->PostMessage(reply);
        free(reply);
      }

      break;
    }
    default:
      break;
  }

  return TRUE;
}

bool SpeechInstance::SendRequest(const char *message) {
  uint32_t size = ((uint32_t)strlen(message));
  uint32_t size_be = htobe32(size);

  if (fd_ == -1) {
    ERR("Socket not connected!");
    return false;
  }

  if (send(fd_, static_cast<void*>(&size_be), sizeof(size_be), 0) < 0) {
    WARN("Failed to send message size: %s", strerror(errno));
    return false;
  }

  void *buf = const_cast<void*>(static_cast<const void *>(message));
  ssize_t len = 0;
  while (size && (len = send(fd_, buf, size, 0)) < size) {
    if (len < 0) {
      WARN("Failed to send message to server: %s", strerror(errno));
      return false;
    }
    size -= len;
    buf = static_cast<char*>(buf) + len;
  }

  return true;
}

uint32_t SpeechInstance::ReadReply(char **reply) {
  uint32_t size_be = 0;

  if (recv(fd_, static_cast<void*>(&size_be),
      sizeof(size_be), MSG_WAITALL) < 0) {
    WARN("Failed read server reply: %s", strerror(errno));
    return 0;
  }

  uint32_t size = be32toh(size_be);
  DBG("Received message size : %u", size);
  char *message = static_cast<char *>(malloc(size + 1));
  memset(message, 0, size);

  // FIXME: loop through till complete read
  if (recv(fd_, message, size, MSG_WAITALL) < 0) {
    WARN("Failed to read server message with size '%u': %s",
        size, strerror(errno));
    free(message);
    return 0;
  }
  message[size] = '\0';

  DBG("Recived message : %s", message);

  if (reply) *reply = message;

  return size;
}

// static
gboolean SpeechInstance::ProcessPendingRepliesCb(gpointer data) {
  SpeechInstance *self = reinterpret_cast<SpeechInstance*>(data);

  if (!self) {
    WARN("asset(self)");
    return FALSE;
  }

  std::string reply = self->pending_replies_.front();
  self->PostMessage(reply.c_str());
  self->pending_replies_.pop();

  if (self->pending_replies_.empty()) {
    self->pending_reply_timer_ = 0;
    return FALSE;
  }

  return TRUE;
}

// static
gboolean SpeechInstance::ProcessPendingRequestsCb(gpointer data) {
  SpeechInstance *self = reinterpret_cast<SpeechInstance*>(data);
  if (!self) {
    WARN("assert(self)");
    return FALSE;
  }

  std::string &request = self->pending_requests_.front();
  if (!self->SendRequest(request.data())) {
    picojson::value in;
    picojson::object out;

    picojson::parse(in, request.data(), request.data() + request.size(), NULL);
    out["reqno"] = in.get("reqno");
    out["error"] = picojson::value("network");
    out["message"] = picojson::value("failed to connect to server");

    self->QueueReply(picojson::value(out).serialize());
  }
  self->pending_requests_.pop();

  if (self->pending_requests_.empty()) {
    self->pending_request_timer_ = 0;
    return FALSE;
  }

  return TRUE;
}

void SpeechInstance::QueueReply(const std::string& reply) {
  pending_replies_.push(reply);
  if (!pending_reply_timer_) {
    pending_reply_timer_ = g_idle_add(ProcessPendingRepliesCb,
        static_cast<gpointer>(this));
  }
}

void SpeechInstance::QueueRequest(const std::string& req) {
  pending_requests_.push(req);
  if (!pending_request_timer_) {
    pending_request_timer_ = g_idle_add(ProcessPendingRequestsCb,
        static_cast<gpointer>(this));
  }
}

void SpeechInstance::HandleSyncMessage(const char* message) {
  picojson::value v, out;
  std::string err;

  DBG("Message: %s", message);

  if (!SendRequest(message)) {
    picojson::object obj;

    obj["error"] = picojson::value("network");
    obj["message"] = picojson::value("server connection failure");
    out = picojson::value(obj);
  } else {
    picojson::parse(v, message, message + strlen(message), &err);
    if (!err.empty())
      return;

    const std::string& req_id = v.get("reqno").to_str();

    do {
      char *reply = NULL;
      uint32_t size;

      if ((size = ReadReply(&reply)) != 0) {
        picojson::parse(out, reply, reply + size, &err);
        free(reply);
        if (!err.empty()) {
          WARN("Failed to read server reply: %s", strerror(errno));
          // TODO(avalluri): fill error details in out
          break;
        } else if (out.get("reqno").to_str() == req_id) {
          break;
        } else {
          QueueReply(out.serialize());
        }
      }
    } while (0);
  }

  SendSyncReply(out.serialize().c_str());
}

void SpeechInstance::HandleMessage(const char* message) {
  if (!message)
    return;

  QueueRequest(message);
}
