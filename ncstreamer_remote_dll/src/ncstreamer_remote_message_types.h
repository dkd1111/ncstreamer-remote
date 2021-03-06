/**
 * Copyright (C) 2017 NCSOFT Corporation
 */


#ifndef NCSTREAMER_REMOTE_DLL_SRC_NCSTREAMER_REMOTE_MESSAGE_TYPES_H_
#define NCSTREAMER_REMOTE_DLL_SRC_NCSTREAMER_REMOTE_MESSAGE_TYPES_H_


namespace ncstreamer {
static const wchar_t *const kNcStreamerWindowTitle{L"NCStreaming"};


class RemoteMessage {
 public:
  enum class MessageType {
    kUndefined = 0,
    kStreamingStatusRequest = 101,
    kStreamingStatusResponse,
    kStreamingStartRequest = 201,
    kStreamingStartResponse,
    kStreamingStartEvent,
    kStreamingStopRequest = 211,
    kStreamingStopResponse,
    kStreamingStopEvent,
    kSettingsQualityUpdateRequest = 301,
    kSettingsQualityUpdateResponse,
    kNcStreamerExitRequest = 901,
    kNcStreamerExitResponse,  // not used.
  };

  class Error {
   public:
    class Start {
     public:
      static const char *const kNoUser;
      static const char *const kNotStandbySelf;
      static const char *const kNotStandbyOther;
      static const char *const kUnknownTitle;
      static const char *const kMePageSelectEmpty;
      static const char *const kPrivacySelectEmpty;
      static const char *const kOwnPageSelectEmpty;
      static const char *const kObsInternal;
      static const char *const kStreamingServiceFacebookLive;
    };
    class Stop {
     public:
      static const char *const kNotOnAir;
      static const char *const kTitleMismatch;
    };
  };
};
}  // namespace ncstreamer


#endif  // NCSTREAMER_REMOTE_DLL_SRC_NCSTREAMER_REMOTE_MESSAGE_TYPES_H_
