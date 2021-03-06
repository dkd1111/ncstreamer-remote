/**
 * Copyright (C) 2017 NCSOFT Corporation
 */


#ifndef NCSTREAMER_REMOTE_DLL_SRC_ERROR_ERROR_CONVERTER_H_
#define NCSTREAMER_REMOTE_DLL_SRC_ERROR_ERROR_CONVERTER_H_


#include <string>
#include <utility>

#include "ncstreamer_remote_dll/include/ncstreamer_remote/error/error_types.h"


namespace ncstreamer_remote {
class ErrorConverter {
 public:
  /// @param error Error code from NCStreamer-Remote.
  /// @return Description of the given error code.
  static std::string
      ToConnectionError(Error::Connection err_code);

  /// @param error Error ID from NCStreamer.
  /// @return Pair of error code and description.
  static std::pair<Error::Start, std::string>
      ToStartError(const std::string &error);

  /// @param error Error ID from NCStreamer.
  /// @return Pair of error code and description.
  static std::pair<Error::Stop, std::string>
      ToStopError(const std::string &error);
};
}  // namespace ncstreamer_remote


#endif  // NCSTREAMER_REMOTE_DLL_SRC_ERROR_ERROR_CONVERTER_H_
