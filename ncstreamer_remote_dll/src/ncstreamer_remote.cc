/**
 * Copyright (C) 2017 NCSOFT Corporation
 */


#include "ncstreamer_remote_dll/include/ncstreamer_remote/ncstreamer_remote.h"

#include <cassert>
#include <codecvt>
#include <locale>
#include <sstream>
#include <unordered_map>

#include "boost/algorithm/string.hpp"
#include "boost/property_tree/json_parser.hpp"

#include "Windows.h"  // NOLINT

#include "ncstreamer_remote_dll/src/error/error_converter.h"
#include "ncstreamer_remote_dll/src/ncstreamer_remote_message_types.h"


namespace {
namespace placeholders = websocketpp::lib::placeholders;
}  // unnamed namespace


namespace ncstreamer_remote {
void NcStreamerRemote::SetUp(uint16_t remote_port) {
  assert(!static_instance);
  static_instance = new NcStreamerRemote{remote_port};
}


void NcStreamerRemote::SetUpDefault() {
  SetUp(9002);
}


void NcStreamerRemote::ShutDown() {
  assert(static_instance);
  delete static_instance;
  static_instance = nullptr;
}


NcStreamerRemote *NcStreamerRemote::Get() {
  assert(static_instance);
  return static_instance;
}


void NcStreamerRemote::RegisterConnectHandler(
    const ConnectHandler &connect_handler) {
  connect_handler_ = connect_handler;
}


void NcStreamerRemote::RegisterDisconnectHandler(
    const DisconnectHandler &disconnect_handler) {
  disconnect_handler_ = disconnect_handler;
}


void NcStreamerRemote::RegisterStartEventHandler(
    const StartEventHandler &start_event_handler) {
  start_event_handler_ = start_event_handler;
}


void NcStreamerRemote::RegisterStopEventHandler(
    const StopEventHandler &stop_event_handler) {
  stop_event_handler_ = stop_event_handler;
}


void NcStreamerRemote::RequestStatus(
    const ErrorHandler &error_handler,
    const StatusResponseHandler &status_response_handler) {
  if (busy_ == true) {
    HandleConnectionError(Error::Connection::kBusy, error_handler);
    return;
  }
  busy_ = true;

  current_error_handler_ = error_handler;
  current_status_response_handler_ = status_response_handler;

  if (!remote_connection_.lock()) {
    Connect([this]() {
      SendStatusRequest();
    });
    return;
  }

  SendStatusRequest();
}


void NcStreamerRemote::RequestStart(
    const std::wstring &title,
    const ErrorHandler &error_handler,
    const StartResponseHandler &start_response_handler) {
  if (busy_ == true) {
    HandleConnectionError(Error::Connection::kBusy, error_handler);
    return;
  }
  busy_ = true;

  current_error_handler_ = error_handler;
  current_start_response_handler_ = start_response_handler;

  if (!remote_connection_.lock()) {
    Connect([this, title]() {
      SendStartRequest(title);
    });
    return;
  }

  SendStartRequest(title);
}


void NcStreamerRemote::RequestStop(
    const std::wstring &title,
    const ErrorHandler &error_handler,
    const StopResponseHandler &stop_response_handler) {
  if (busy_ == true) {
    HandleConnectionError(Error::Connection::kBusy, error_handler);
    return;
  }
  busy_ = true;

  current_error_handler_ = error_handler;
  current_stop_response_handler_ = stop_response_handler;

  if (!remote_connection_.lock()) {
    Connect([this, title]() {
      SendStopRequest(title);
    });
    return;
  }

  SendStopRequest(title);
}


void NcStreamerRemote::RequestQualityUpdate(
    const std::wstring &quality,
    const ErrorHandler &error_handler,
    const SuccessHandler &quality_update_response_handler) {
  if (busy_ == true) {
    HandleConnectionError(Error::Connection::kBusy, error_handler);
    return;
  }
  busy_ = true;

  current_error_handler_ = error_handler;
  current_quality_update_response_handler_ = quality_update_response_handler;

  if (!remote_connection_.lock()) {
    Connect([this, quality]() {
      SendQualityUpdateRequest(quality);
    });
    return;
  }

  SendQualityUpdateRequest(quality);
}


void NcStreamerRemote::RequestExit(
    const ErrorHandler &error_handler) {
  if (busy_ == true) {
    HandleConnectionError(Error::Connection::kBusy, error_handler);
    return;
  }
  busy_ = true;

  current_error_handler_ = error_handler;

  if (!remote_connection_.lock()) {
    Connect([this]() {
      SendExitRequest();
    });
    return;
  }

  SendExitRequest();
}


NcStreamerRemote::NcStreamerRemote(uint16_t remote_port)
    : remote_uri_{new websocketpp::uri{false, "localhost", remote_port, ""}},
      io_service_{},
      io_service_work_{io_service_},
      remote_{},
      remote_threads_{},
      remote_log_{},
      remote_connection_{},
      timer_to_keep_connected_{io_service_},
      busy_{},
      connect_handler_{},
      disconnect_handler_{},
      start_event_handler_{},
      stop_event_handler_{},
      current_error_handler_{},
      current_status_response_handler_{},
      current_start_response_handler_{},
      current_stop_response_handler_{},
      current_quality_update_response_handler_{} {
  busy_ = false;

  remote_log_.open("ncstreamer_remote.log");
  remote_.set_access_channels(websocketpp::log::alevel::all);
  remote_.set_access_channels(websocketpp::log::elevel::all);
  remote_.get_alog().set_ostream(&remote_log_);
  remote_.get_elog().set_ostream(&remote_log_);

  websocketpp::lib::error_code ec;
  remote_.init_asio(&io_service_, ec);
  if (ec) {
    HandleError(Error::Connection::kRemoteInitAsio, ec);
    assert(false);
    return;
  }

  remote_.set_fail_handler(websocketpp::lib::bind(
      &NcStreamerRemote::OnRemoteFail, this, placeholders::_1));
  remote_.set_close_handler(websocketpp::lib::bind(
      &NcStreamerRemote::OnRemoteClose, this, placeholders::_1));
  remote_.set_message_handler(websocketpp::lib::bind(
      &NcStreamerRemote::OnRemoteMessage, this,
          placeholders::_1, placeholders::_2));

  static const std::size_t kRemoteThreadsSize{1};  // just one enough.
  for (std::size_t i = 0; i < kRemoteThreadsSize; ++i) {
    remote_threads_.emplace_back([this]() {
      remote_.run();
    });
  }

  KeepConnected();
}


NcStreamerRemote::~NcStreamerRemote() {
  remote_.stop();
  for (auto &t : remote_threads_) {
    if (t.joinable() == true) {
      t.join();
    }
  }
}


bool NcStreamerRemote::ExistsNcStreamer() {
  HWND wnd = ::FindWindow(nullptr, ncstreamer::kNcStreamerWindowTitle);
  return (wnd != NULL);
}


void NcStreamerRemote::KeepConnected() {
  if (remote_connection_.lock()) {
    return;
  }

  Connect([this](
      ErrorCategory err_category,
      int err_code,
      const std::wstring &err_msg) {
    timer_to_keep_connected_.expires_from_now(Chrono::seconds{1});
    timer_to_keep_connected_.async_wait([this](
        const boost::system::error_code &ec) {
      if (ec) {
        return;
      }
      KeepConnected();
    });
  }, [this]() {
    if (connect_handler_) {
      connect_handler_();
    }
  });
}


void NcStreamerRemote::Connect(
    const OpenHandler &open_handler) {
  Connect(current_error_handler_, open_handler);
}


void NcStreamerRemote::Connect(
    const ErrorHandler &error_handler,
    const OpenHandler &open_handler) {
  if (ExistsNcStreamer() == false) {
    HandleError(Error::Connection::kNoNcStreamer, error_handler);
    return;
  }

  websocketpp::lib::error_code ec;
  auto connection = remote_.get_connection(remote_uri_, ec);
  if (ec) {
    HandleError(Error::Connection::kRemoteConnect, ec, error_handler);
    return;
  }

  remote_.connect(connection);
  connection->set_open_handler([this, open_handler](
      websocketpp::connection_hdl connection) {
    remote_connection_ = connection;
    open_handler();
  });
}


void NcStreamerRemote::SendStatusRequest() {
  std::stringstream msg;
  {
    boost::property_tree::ptree tree;
    tree.put("type", static_cast<int>(
        ncstreamer::RemoteMessage::MessageType::kStreamingStatusRequest));
    boost::property_tree::write_json(msg, tree, false);
  }

  websocketpp::lib::error_code ec;
  remote_.send(
      remote_connection_, msg.str(), websocketpp::frame::opcode::text, ec);
  if (ec) {
    HandleError(Error::Connection::kRemoteSend, ec);
    return;
  }
}


void NcStreamerRemote::SendStartRequest(const std::wstring &title) {
  std::stringstream msg;
  {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

    boost::property_tree::ptree tree;
    tree.put("type", static_cast<int>(
        ncstreamer::RemoteMessage::MessageType::kStreamingStartRequest));
    tree.put("title", converter.to_bytes(title));
    boost::property_tree::write_json(msg, tree, false);
  }

  websocketpp::lib::error_code ec;
  remote_.send(
      remote_connection_, msg.str(), websocketpp::frame::opcode::text, ec);
  if (ec) {
    HandleError(Error::Connection::kRemoteSend, ec);
    return;
  }
}


void NcStreamerRemote::SendStopRequest(const std::wstring &title) {
  std::stringstream msg;
  {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

    boost::property_tree::ptree tree;
    tree.put("type", static_cast<int>(
        ncstreamer::RemoteMessage::MessageType::kStreamingStopRequest));
    tree.put("title", converter.to_bytes(title));
    boost::property_tree::write_json(msg, tree, false);
  }

  websocketpp::lib::error_code ec;
  remote_.send(
      remote_connection_, msg.str(), websocketpp::frame::opcode::text, ec);
  if (ec) {
    HandleError(Error::Connection::kRemoteSend, ec);
    return;
  }
}


void NcStreamerRemote::SendQualityUpdateRequest(const std::wstring &quality) {
  std::stringstream msg;
  {
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

    boost::property_tree::ptree tree;
    tree.put("type", static_cast<int>(
        ncstreamer::RemoteMessage::MessageType::kSettingsQualityUpdateRequest));
    tree.put("quality", converter.to_bytes(quality));
    boost::property_tree::write_json(msg, tree, false);
  }

  websocketpp::lib::error_code ec;
  remote_.send(
      remote_connection_, msg.str(), websocketpp::frame::opcode::text, ec);
  if (ec) {
    HandleError(Error::Connection::kRemoteSend, ec);
    return;
  }
}


void NcStreamerRemote::SendExitRequest() {
  std::stringstream msg;
  {
    boost::property_tree::ptree tree;
    tree.put("type", static_cast<int>(
        ncstreamer::RemoteMessage::MessageType::kNcStreamerExitRequest));
    boost::property_tree::write_json(msg, tree, false);
  }

  websocketpp::lib::error_code ec;
  remote_.send(
      remote_connection_, msg.str(), websocketpp::frame::opcode::text, ec);
  if (ec) {
    HandleError(Error::Connection::kRemoteSend, ec);
    return;
  }
}


void NcStreamerRemote::OnRemoteFail(websocketpp::connection_hdl connection) {
  HandleDisconnect(Error::Connection::kOnRemoteFail);
}


void NcStreamerRemote::OnRemoteClose(websocketpp::connection_hdl connection) {
  HandleDisconnect(Error::Connection::kOnRemoteClose);
}


void NcStreamerRemote::OnRemoteMessage(
    websocketpp::connection_hdl connection,
    websocketpp::connection<AsioClient>::message_ptr msg) {
  busy_ = false;

  boost::property_tree::ptree response;
  ncstreamer::RemoteMessage::MessageType msg_type{
      ncstreamer::RemoteMessage::MessageType::kUndefined};

  std::stringstream ss{msg->get_payload()};
  try {
    boost::property_tree::read_json(ss, response);
    msg_type = static_cast<ncstreamer::RemoteMessage::MessageType>(
        response.get<int>("type"));
  } catch (const std::exception &/*e*/) {
    msg_type = ncstreamer::RemoteMessage::MessageType::kUndefined;
  }

  using MessageHandler = std::function<void(
      const boost::property_tree::ptree &/*response*/)>;
  static const std::unordered_map<ncstreamer::RemoteMessage::MessageType,
                                  MessageHandler> kMessageHandlers{
      {ncstreamer::RemoteMessage::MessageType::kStreamingStartEvent,
       std::bind(&NcStreamerRemote::OnRemoteStartEvent,
           this, std::placeholders::_1)},
      {ncstreamer::RemoteMessage::MessageType::kStreamingStopEvent,
       std::bind(&NcStreamerRemote::OnRemoteStopEvent,
           this, std::placeholders::_1)},
      {ncstreamer::RemoteMessage::MessageType::kStreamingStatusResponse,
       std::bind(&NcStreamerRemote::OnRemoteStatusResponse,
           this, std::placeholders::_1)},
      {ncstreamer::RemoteMessage::MessageType::kStreamingStartResponse,
       std::bind(&NcStreamerRemote::OnRemoteStartResponse,
           this, std::placeholders::_1)},
      {ncstreamer::RemoteMessage::MessageType::kStreamingStopResponse,
       std::bind(&NcStreamerRemote::OnRemoteStopResponse,
           this, std::placeholders::_1)},
      {ncstreamer::RemoteMessage::MessageType::kSettingsQualityUpdateResponse,
       std::bind(&NcStreamerRemote::OnRemoteQualityUpdateResponse,
           this, std::placeholders::_1)}};

  auto i = kMessageHandlers.find(msg_type);
  if (i == kMessageHandlers.end()) {
    LogWarning(
        "unknown message type: " + std::to_string(static_cast<int>(msg_type)));
    return;
  }
  i->second(response);
}


void NcStreamerRemote::OnRemoteStartEvent(
    const boost::property_tree::ptree &evt) {
  if (!start_event_handler_) {
    return;
  }

  std::string source{};
  std::string user_page{};
  std::string privacy{};
  std::string description{};
  std::string mic{};
  std::string service_provider{};
  std::string stream_url{};
  std::string post_url{};
  try {
    source = evt.get<std::string>("source");
    user_page = evt.get<std::string>("userPage");
    privacy = evt.get<std::string>("privacy");
    description = evt.get<std::string>("description");
    mic = evt.get<std::string>("mic");
    service_provider = evt.get<std::string>("serviceProvider");
    stream_url = evt.get<std::string>("streamUrl");
    post_url = evt.get<std::string>("postUrl");
  } catch (const std::exception &/*e*/) {
    source.clear();
    user_page.clear();
    privacy.clear();
    description.clear();
    mic.clear();
    service_provider.clear();
    stream_url.clear();
    post_url.clear();
  }

  if (source.empty() == true) {
    LogError("source.empty()");
    return;
  }

  std::vector<std::string> tokens{};
  boost::split(tokens, source, boost::is_any_of(":"));
  const std::string &source_title = tokens.at(0);

  static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
  start_event_handler_(
      converter.from_bytes(source_title),
      converter.from_bytes(user_page),
      converter.from_bytes(privacy),
      converter.from_bytes(description),
      converter.from_bytes(mic),
      converter.from_bytes(service_provider),
      converter.from_bytes(stream_url),
      converter.from_bytes(post_url));
}


void NcStreamerRemote::OnRemoteStopEvent(
    const boost::property_tree::ptree &evt) {
  if (!stop_event_handler_) {
    return;
  }

  std::string source{};
  try {
    source = evt.get<std::string>("source");
  } catch (const std::exception &/*e*/) {
    source.clear();
  }

  if (source.empty() == true) {
    LogError("source.empty()");
    return;
  }

  std::vector<std::string> tokens{};
  boost::algorithm::split(tokens, source, boost::is_any_of(":"));
  const std::string &source_title = tokens.at(0);

  static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
  stop_event_handler_(
      converter.from_bytes(source_title));
}


void NcStreamerRemote::OnRemoteStatusResponse(
    const boost::property_tree::ptree &response) {
  std::string status{};
  std::string source_title{};
  std::string user_name{};
  std::string quality{};
  try {
    status = response.get<std::string>("status");
    source_title = response.get<std::string>("sourceTitle");
    user_name = response.get<std::string>("userName");
    quality = response.get<std::string>("quality");
  } catch (const std::exception &/*e*/) {
    status.clear();
    source_title.clear();
  }

  if (status.empty() == true) {
    LogError("status.empty()");
    return;
  }
  if (quality.empty() == true) {
    LogError("quality.empty()");
    return;
  }

  if (!current_status_response_handler_) {
    LogError("!current_status_response_handler_");
    return;
  }

  static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
  current_status_response_handler_(
      converter.from_bytes(status),
      converter.from_bytes(source_title),
      converter.from_bytes(user_name),
      converter.from_bytes(quality));
}


void NcStreamerRemote::OnRemoteStartResponse(
    const boost::property_tree::ptree &response) {
  bool exception_occurred{false};
  std::string error{};
  try {
    error = response.get<std::string>("error");
  } catch (const std::exception &/*e*/) {
    exception_occurred = true;
  }

  if (exception_occurred == true) {
    LogError("start response broken");
    return;
  }

  if (error.empty() == false) {
    static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    const auto &err_info = ErrorConverter::ToStartError(error);

    current_error_handler_(
        ErrorCategory::kStart,
        static_cast<int>(err_info.first),
        converter.from_bytes(err_info.second));
  } else {
    current_start_response_handler_(true);
  }
}


void NcStreamerRemote::OnRemoteStopResponse(
    const boost::property_tree::ptree &response) {
  bool exception_occurred{false};
  std::string error{};
  try {
    error = response.get<std::string>("error");
  } catch (const std::exception &/*e*/) {
    exception_occurred = true;
  }

  if (exception_occurred == true) {
    LogError("stop response broken");
    return;
  }

  if (error.empty() == false) {
    static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    const auto &err_info = ErrorConverter::ToStopError(error);

    current_error_handler_(
        ErrorCategory::kStop,
        static_cast<int>(err_info.first),
        converter.from_bytes(err_info.second));
  } else {
    current_stop_response_handler_(true);
  }
}


void NcStreamerRemote::OnRemoteQualityUpdateResponse(
    const boost::property_tree::ptree &response) {
  bool exception_occurred{false};
  std::string error{};
  try {
    error = response.get<std::string>("error");
  } catch (const std::exception &/*e*/) {
    exception_occurred = true;
  }

  if (exception_occurred == true) {
    LogError("stop response broken");
    return;
  }

  if (error.empty() == false) {
    static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    current_error_handler_(
        ErrorCategory::kMisc, 0, converter.from_bytes(error));
  } else {
    current_quality_update_response_handler_(true);
  }
}


void NcStreamerRemote::HandleDisconnect(
    Error::Connection err_code) {
  remote_connection_.reset();
  busy_ = false;
  LogWarning(ErrorConverter::ToConnectionError(err_code));

  if (disconnect_handler_) {
    disconnect_handler_();
  }

  KeepConnected();
}


void NcStreamerRemote::HandleConnectionError(
    Error::Connection err_code,
    const ErrorHandler &err_handler) {
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

  err_handler(
      ErrorCategory::kConnection,
      static_cast<int>(err_code),
      converter.from_bytes(ErrorConverter::ToConnectionError(err_code)));
}


void NcStreamerRemote::HandleError(
    Error::Connection err_code,
    const websocketpp::lib::error_code &ec) {
  HandleError(err_code, ec, current_error_handler_);
}


void NcStreamerRemote::HandleError(
    Error::Connection err_code,
    const websocketpp::lib::error_code &ec,
    const ErrorHandler &err_handler) {
  const auto &err_msg = ErrorConverter::ToConnectionError(err_code);
  std::stringstream ss;
  ss << err_msg << ": " << ec.message();
  HandleError(err_code, ss.str(), err_handler);
}


void NcStreamerRemote::HandleError(
    Error::Connection err_code) {
  const auto &err_msg = ErrorConverter::ToConnectionError(err_code);
  HandleError(err_code, err_msg, current_error_handler_);
}


void NcStreamerRemote::HandleError(
    Error::Connection err_code,
    const std::string &err_msg,
    const ErrorHandler &err_handler) {
  busy_ = false;
  LogError(err_msg);

  if (err_handler) {
    static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    err_handler(
        ErrorCategory::kConnection,
        static_cast<int>(err_code),
        converter.from_bytes(err_msg));
  }
}


void NcStreamerRemote::HandleError(
    Error::Connection err_code,
    const ErrorHandler &err_handler) {
  const auto &err_msg = ErrorConverter::ToConnectionError(err_code);
  HandleError(err_code, err_msg, err_handler);
}


void NcStreamerRemote::LogWarning(const std::string &warn_msg) {
  remote_.get_elog().write(websocketpp::log::elevel::warn, warn_msg);
}


void NcStreamerRemote::LogError(const std::string &err_msg) {
  remote_.get_elog().write(websocketpp::log::elevel::rerror, err_msg);
}


NcStreamerRemote *NcStreamerRemote::static_instance{nullptr};
}  // namespace ncstreamer_remote
