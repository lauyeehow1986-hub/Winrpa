#ifndef R_NO_REMAP
#define R_NO_REMAP
#endif
#include <cpp11.hpp>
#include "cdp_client.h"
#include "cdp_json.h"
#include "mta_worker.h"
#include "winrpa_core.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winhttp.h>

namespace winrpa {

namespace {

std::int64_t steady_now_ms() noexcept {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

struct WinHttpHandle {
  HINTERNET h = nullptr;
  WinHttpHandle() noexcept = default;
  explicit WinHttpHandle(HINTERNET handle) noexcept : h(handle) {}
  WinHttpHandle(const WinHttpHandle&) = delete;
  WinHttpHandle& operator=(const WinHttpHandle&) = delete;
  WinHttpHandle(WinHttpHandle&& other) noexcept : h(other.h) { other.h = nullptr; }
  WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
    if (this != &other) {
      reset();
      h = other.h;
      other.h = nullptr;
    }
    return *this;
  }
  ~WinHttpHandle() { reset(); }
  void reset(HINTERNET next = nullptr) noexcept {
    if (h != nullptr) {
      WinHttpCloseHandle(h);
    }
    h = next;
  }
  HINTERNET detach() noexcept {
    HINTERNET cur = h;
    h = nullptr;
    return cur;
  }
  explicit operator bool() const noexcept { return h != nullptr; }
};

std::wstring quote_cmd_arg(const std::wstring& arg) {
  std::wstring out = L"\"";
  for (wchar_t ch : arg) {
    if (ch == L'"') {
      out += L"\\\"";
    } else {
      out.push_back(ch);
    }
  }
  out.push_back(L'"');
  return out;
}

std::string find_browser_executable(const std::string& pref) {
  std::string p = pref;
  for (char& c : p) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }

  auto env_path = [](const wchar_t* name) -> std::wstring {
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0U);
    if (needed <= 1U) {
      return {};
    }
    std::wstring buf(static_cast<std::size_t>(needed), L'\0');
    const DWORD written = GetEnvironmentVariableW(name, buf.data(), needed);
    if (written == 0U) {
      return {};
    }
    buf.resize(static_cast<std::size_t>(written));
    return buf;
  };

  const std::wstring pf86 = env_path(L"ProgramFiles(x86)");
  const std::wstring pf = env_path(L"ProgramFiles");
  const std::wstring local_app = env_path(L"LOCALAPPDATA");

  std::vector<std::wstring> edge_paths;
  if (!pf86.empty()) edge_paths.push_back(pf86 + L"\\Microsoft\\Edge\\Application\\msedge.exe");
  if (!pf.empty()) edge_paths.push_back(pf + L"\\Microsoft\\Edge\\Application\\msedge.exe");
  if (!local_app.empty()) edge_paths.push_back(local_app + L"\\Microsoft\\Edge\\Application\\msedge.exe");

  std::vector<std::wstring> chrome_paths;
  if (!pf.empty()) chrome_paths.push_back(pf + L"\\Google\\Chrome\\Application\\chrome.exe");
  if (!pf86.empty()) chrome_paths.push_back(pf86 + L"\\Google\\Chrome\\Application\\chrome.exe");
  if (!local_app.empty()) chrome_paths.push_back(local_app + L"\\Google\\Chrome\\Application\\chrome.exe");

  std::vector<std::wstring> candidates;
  if (p == "chrome") {
    candidates = std::move(chrome_paths);
  } else if (p == "edge" || p == "msedge") {
    candidates = std::move(edge_paths);
  } else {
    candidates.insert(candidates.end(), edge_paths.begin(), edge_paths.end());
    candidates.insert(candidates.end(), chrome_paths.begin(), chrome_paths.end());
  }

  std::error_code ec;
  for (const auto& wpath : candidates) {
    if (!wpath.empty() && std::filesystem::is_regular_file(std::filesystem::path(wpath), ec)) {
      return wide_to_utf8(wpath.c_str(), static_cast<int>(wpath.size()));
    }
  }
  return "";
}

struct HttpResponse {
  int status = 0;
  std::string body;
};

bool http_loopback_request(
    const std::string& host,
    int port,
    const wchar_t* method,
    const std::wstring& path,
    int timeout_ms,
    HttpResponse& resp_out) {
  resp_out = HttpResponse{};
  WinHttpHandle hSess(WinHttpOpen(
      L"winrpa-cdp/0.1",
      WINHTTP_ACCESS_TYPE_NO_PROXY,
      WINHTTP_NO_PROXY_NAME,
      WINHTTP_NO_PROXY_BYPASS,
      0U));
  if (!hSess) {
    return false;
  }
  const int t = timeout_ms > 0 ? timeout_ms : 1500;
  WinHttpSetTimeouts(hSess.h, t, t, t, t);

  const std::wstring whost = utf8_to_wide(host);
  WinHttpHandle hConn(WinHttpConnect(
      hSess.h, whost.c_str(), static_cast<INTERNET_PORT>(port), 0U));
  if (!hConn) {
    return false;
  }

  WinHttpHandle hReq(WinHttpOpenRequest(
      hConn.h, method, path.c_str(), nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0U));
  if (!hReq) {
    return false;
  }

  if (!WinHttpSendRequest(
          hReq.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0U,
          WINHTTP_NO_REQUEST_DATA, 0U, 0U, 0U)) {
    return false;
  }
  if (!WinHttpReceiveResponse(hReq.h, nullptr)) {
    return false;
  }

  DWORD status_code = 0U;
  DWORD sz = sizeof(status_code);
  if (WinHttpQueryHeaders(
          hReq.h,
          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
          WINHTTP_HEADER_NAME_BY_INDEX,
          &status_code,
          &sz,
          WINHTTP_NO_HEADER_INDEX)) {
    resp_out.status = static_cast<int>(status_code);
  }

  DWORD avail = 0U;
  while (WinHttpQueryDataAvailable(hReq.h, &avail) && avail > 0U) {
    std::string chunk(static_cast<std::size_t>(avail), '\0');
    DWORD read_bytes = 0U;
    if (!WinHttpReadData(hReq.h, chunk.data(), avail, &read_bytes)) {
      break;
    }
    chunk.resize(static_cast<std::size_t>(read_bytes));
    resp_out.body += chunk;
    if (resp_out.body.size() > 8U * 1024U * 1024U) {
      break;
    }
  }
  return resp_out.status >= 100;
}

int pick_free_debug_port() {
  const int pid = static_cast<int>(GetCurrentProcessId());
  const int base = 19222 + ((pid * 37) % 9000);
  for (int offset = 0; offset < 64; ++offset) {
    const int candidate = base + offset;
    HttpResponse resp;
    if (!http_loopback_request("127.0.0.1", candidate, L"GET", L"/json/version", 120, resp)) {
      return candidate;
    }
  }
  return 19222;
}

bool parse_ws_url(
    const std::string& ws_url,
    std::string& host_out,
    int& port_out,
    std::string& path_out) {
  const std::string prefix = "ws://";
  if (ws_url.compare(0U, prefix.size(), prefix) != 0) {
    return false;
  }
  const std::string rest = ws_url.substr(prefix.size());
  const std::size_t slash = rest.find('/');
  const std::string authority = (slash == std::string::npos) ? rest : rest.substr(0U, slash);
  path_out = (slash == std::string::npos) ? "/" : rest.substr(slash);

  const std::size_t colon = authority.rfind(':');
  if (colon != std::string::npos) {
    host_out = authority.substr(0U, colon);
    port_out = std::stoi(authority.substr(colon + 1U));
  } else {
    host_out = authority;
    port_out = 80;
  }
  return !host_out.empty() && port_out > 0;
}

std::string find_page_ws_url(
    const std::string& host,
    int port,
    int target_index,
    bool create_if_missing,
    std::string& err_out) {
  HttpResponse resp;
  if (!http_loopback_request(host, port, L"GET", L"/json/list", 1500, resp)) {
    err_out = "Unable to connect to browser CDP HTTP endpoint.";
    return "";
  }
  if (resp.status != 200) {
    err_out = "CDP /json/list returned HTTP status " + std::to_string(resp.status) + ".";
    return "";
  }

  try {
    const cdp::JsonValue parsed = cdp::parse_json(resp.body);
    if (parsed.is_array()) {
      std::vector<std::string> urls;
      for (const auto& item : parsed.arr_val) {
        if (!item.is_object()) continue;
        if (item.get_string("type") == "page") {
          const std::string ws = item.get_string("webSocketDebuggerUrl");
          if (!ws.empty()) {
            urls.push_back(ws);
          }
        }
      }
      if (!urls.empty()) {
        const std::size_t idx = target_index <= 1
                                    ? 0U
                                    : static_cast<std::size_t>(target_index - 1);
        if (idx < urls.size()) {
          return urls[idx];
        }
        err_out = "Requested target_index exceeds open browser page count.";
        return "";
      }
    }
  } catch (const std::exception& e) {
    err_out = std::string("Invalid /json/list JSON: ") + e.what();
    return "";
  }

  if (create_if_missing) {
    HttpResponse new_resp;
    if (http_loopback_request(host, port, L"PUT", L"/json/new?about:blank", 2000, new_resp) &&
        new_resp.status == 200) {
      try {
        const cdp::JsonValue obj = cdp::parse_json(new_resp.body);
        const std::string ws = obj.get_string("webSocketDebuggerUrl");
        if (!ws.empty()) {
          return ws;
        }
      } catch (...) {
      }
    }
  }
  err_out = "No inspectable page targets exposed by browser.";
  return "";
}

class CdpSession;

class SessionRegistry {
 public:
  static SessionRegistry& instance() {
    static SessionRegistry reg;
    return reg;
  }

  void add(CdpSession* s) {
    std::lock_guard<std::mutex> lock(mu_);
    sessions_.insert(s);
  }

  void remove(CdpSession* s) {
    std::lock_guard<std::mutex> lock(mu_);
    sessions_.erase(s);
  }

  std::vector<CdpSession*> snapshot() {
    std::lock_guard<std::mutex> lock(mu_);
    return std::vector<CdpSession*>(sessions_.begin(), sessions_.end());
  }

 private:
  std::mutex mu_;
  std::unordered_set<CdpSession*> sessions_;
};

class CdpSession {
 public:
  struct PendingReply {
    bool done = false;
    bool has_error = false;
    std::string result_json;
    std::string error_message;
  };

  struct EventSnapshot {
    std::int64_t dom_seq = 0;
    std::int64_t load_seq = 0;
    std::int64_t net_idle_seq = 0;
    std::int64_t net_almost_idle_seq = 0;
    int in_flight = 0;
    double ms_since_net = 0.0;
    bool connected = false;
  };

  CdpSession() {
    last_net_activity_ms_ = steady_now_ms();
    SessionRegistry::instance().add(this);
  }

  ~CdpSession() {
    SessionRegistry::instance().remove(this);
    teardown(true);
  }

  CdpSession(const CdpSession&) = delete;
  CdpSession& operator=(const CdpSession&) = delete;

  void attach_browser_process(HANDLE proc, std::string profile_dir) {
    std::lock_guard<std::mutex> lock(mu_);
    proc_handle_ = proc;
    profile_dir_ = std::move(profile_dir);
  }

  void open_websocket(const std::string& ws_url) {
    std::string host;
    int port = 0;
    std::string path;
    if (!parse_ws_url(ws_url, host, port, path)) {
      throw std::runtime_error("Unsupported WebSocket URL: " + ws_url);
    }

    WinHttpHandle hSess(WinHttpOpen(
        L"winrpa-cdp/0.1",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0U));
    if (!hSess) {
      throw std::runtime_error("WinHttpOpen failed for CDP WebSocket.");
    }
    WinHttpSetTimeouts(hSess.h, 5000, 5000, 10000, 0);

    const std::wstring whost = utf8_to_wide(host);
    WinHttpHandle hConn(WinHttpConnect(
        hSess.h, whost.c_str(), static_cast<INTERNET_PORT>(port), 0U));
    if (!hConn) {
      throw std::runtime_error("WinHttpConnect failed for CDP WebSocket.");
    }

    const std::wstring wpath = utf8_to_wide(path);
    WinHttpHandle hReq(WinHttpOpenRequest(
        hConn.h, L"GET", wpath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0U));
    if (!hReq) {
      throw std::runtime_error("WinHttpOpenRequest failed for CDP WebSocket.");
    }

    if (!WinHttpSetOption(
            hReq.h, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0U)) {
      throw std::runtime_error("Failed to enable WebSocket upgrade option.");
    }

    if (!WinHttpSendRequest(
            hReq.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0U,
            WINHTTP_NO_REQUEST_DATA, 0U, 0U, 0U) ||
        !WinHttpReceiveResponse(hReq.h, nullptr)) {
      throw std::runtime_error("WebSocket HTTP handshake failed.");
    }

    DWORD status = 0U;
    DWORD ssz = sizeof(status);
    WinHttpQueryHeaders(
        hReq.h,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status,
        &ssz,
        WINHTTP_NO_HEADER_INDEX);
    if (status != 101U) {
      throw std::runtime_error(
          "CDP WebSocket handshake rejected with HTTP " + std::to_string(status) + ".");
    }

    WinHttpHandle hWs(WinHttpWebSocketCompleteUpgrade(hReq.h, 0U));
    if (!hWs) {
      throw std::runtime_error("WinHttpWebSocketCompleteUpgrade failed.");
    }
    hReq.reset();

    {
      std::lock_guard<std::mutex> lock(mu_);
      h_session_ = hSess.detach();
      h_conn_ = hConn.detach();
      h_ws_ = hWs.detach();
      connected_ = true;
      stopping_ = false;
      reader_thread_ = std::thread(&CdpSession::reader_loop, this);
    }

    // Enable Page, DOM, Runtime, and Network tracking on attach.
    send_command("Page.enable", "{}", 8000);
    send_command("Page.setLifecycleEventsEnabled", "{\"enabled\":true}", 8000);
    send_command("DOM.enable", "{}", 8000);
    send_command("Runtime.enable", "{}", 8000);
    send_command("Network.enable", "{}", 8000);
  }

  std::string send_command(
      const std::string& method,
      const std::string& params_json,
      int timeout_ms) {
    const std::string params_clean = params_json.empty() ? "{}" : params_json;
    const std::int64_t id = next_id_.fetch_add(1LL, std::memory_order_relaxed);
    std::shared_ptr<PendingReply> slot = std::make_shared<PendingReply>();

    HINTERNET ws = nullptr;
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (!connected_ || h_ws_ == nullptr) {
        throw std::runtime_error("CDP WebSocket session is closed or disconnected.");
      }
      ws = h_ws_;
      pending_[id] = slot;
    }

    const std::string frame =
        "{\"id\":" + std::to_string(id) +
        ",\"method\":" + cdp::json_escape(method) +
        ",\"params\":" + params_clean + "}";

    DWORD send_err = ERROR_SUCCESS;
    {
      std::lock_guard<std::mutex> send_lock(send_mu_);
      send_err = WinHttpWebSocketSend(
          ws,
          WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
          const_cast<char*>(frame.data()),
          static_cast<DWORD>(frame.size()));
    }
    if (send_err != ERROR_SUCCESS) {
      std::lock_guard<std::mutex> lock(mu_);
      pending_.erase(id);
      throw std::runtime_error(
          "WinHttpWebSocketSend failed (Win32 error " + std::to_string(send_err) + ").");
    }

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 15000);

    for (;;) {
      check_abort_or_interrupt();
      std::unique_lock<std::mutex> lock(mu_);
      cv_.wait_for(lock, std::chrono::milliseconds(50), [&]() {
        return slot->done || !connected_ || stopping_;
      });
      if (slot->done) {
        pending_.erase(id);
        if (slot->has_error) {
          throw std::runtime_error(slot->error_message);
        }
        return slot->result_json;
      }
      if (!connected_ || stopping_) {
        pending_.erase(id);
        throw std::runtime_error("CDP connection closed while waiting for " + method + ".");
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        pending_.erase(id);
        throw std::runtime_error("Timed out waiting for CDP reply to " + method + ".");
      }
    }
  }

  EventSnapshot event_state() {
    std::lock_guard<std::mutex> lock(mu_);
    EventSnapshot s;
    s.dom_seq = dom_seq_;
    s.load_seq = load_seq_;
    s.net_idle_seq = net_idle_seq_;
    s.net_almost_idle_seq = net_almost_idle_seq_;
    s.in_flight = static_cast<int>(inflight_reqs_.size());
    const std::int64_t delta = steady_now_ms() - last_net_activity_ms_;
    s.ms_since_net = static_cast<double>(delta >= 0 ? delta : 0LL);
    s.connected = connected_;
    return s;
  }

  void close() {
    teardown(false);
  }

 private:
  void reader_loop() {
    std::string accumulated;
    std::vector<char> chunk(65536U);

    for (;;) {
      DWORD bytes_read = 0U;
      WINHTTP_WEB_SOCKET_BUFFER_TYPE buf_type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
      const DWORD err = WinHttpWebSocketReceive(
          h_ws_,
          chunk.data(),
          static_cast<DWORD>(chunk.size()),
          &bytes_read,
          &buf_type);
      if (err != ERROR_SUCCESS || buf_type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
        break;
      }
      if (bytes_read > 0U) {
        accumulated.append(chunk.data(), static_cast<std::size_t>(bytes_read));
      }
      if (accumulated.size() > 128U * 1024U * 1024U) {
        accumulated.clear();
        break;
      }
      if (buf_type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
        handle_incoming_message(accumulated);
        accumulated.clear();
      }
    }

    std::lock_guard<std::mutex> lock(mu_);
    connected_ = false;
    for (auto& kv : pending_) {
      kv.second->done = true;
      kv.second->has_error = true;
      kv.second->error_message = "CDP WebSocket disconnected.";
    }
    cv_.notify_all();
  }

  void handle_incoming_message(const std::string& raw) {
    try {
      const cdp::JsonValue msg = cdp::parse_json(raw);
      if (!msg.is_object()) {
        return;
      }

      const cdp::JsonValue* id_val = msg.find("id");
      if (id_val != nullptr && id_val->is_number()) {
        const std::int64_t id = id_val->is_int ? id_val->int_val
                                               : static_cast<std::int64_t>(id_val->num_val);
        const cdp::JsonValue* err_val = msg.find("error");
        const cdp::JsonValue* res_val = msg.find("result");

        std::lock_guard<std::mutex> lock(mu_);
        auto it = pending_.find(id);
        if (it != pending_.end()) {
          if (err_val != nullptr && err_val->is_object()) {
            it->second->has_error = true;
            it->second->error_message =
                "CDP error: " + err_val->get_string("message", "Unknown CDP error");
          } else {
            it->second->result_json = (res_val != nullptr) ? cdp::dump_json(*res_val) : "{}";
          }
          it->second->done = true;
          cv_.notify_all();
        }
        return;
      }

      const std::string method = msg.get_string("method");
      if (method.empty()) {
        return;
      }
      const cdp::JsonValue* params = msg.find("params");

      std::lock_guard<std::mutex> lock(mu_);
      if (method == "Page.domContentEventFired") {
        ++dom_seq_;
        cv_.notify_all();
      } else if (method == "Page.loadEventFired") {
        ++load_seq_;
        cv_.notify_all();
      } else if (method == "Page.lifecycleEvent" && params != nullptr) {
        const std::string ev_name = params->get_string("name");
        if (ev_name == "networkIdle") {
          ++net_idle_seq_;
          last_net_activity_ms_ = steady_now_ms();
          cv_.notify_all();
        } else if (ev_name == "networkAlmostIdle") {
          ++net_almost_idle_seq_;
          cv_.notify_all();
        }
      } else if (method == "Page.frameNavigated" && params != nullptr) {
        const cdp::JsonValue* frame = params->find("frame");
        if (frame != nullptr && frame->is_object() &&
            frame->get_string("parentId").empty()) {
          // Clear stale requests from previous top-level document navigations.
          inflight_reqs_.clear();
          last_net_activity_ms_ = steady_now_ms();
          cv_.notify_all();
        }
      } else if (method == "Network.requestWillBeSent" && params != nullptr) {
        const std::string req_id = params->get_string("requestId");
        const std::string rtype = params->get_string("type");
        const cdp::JsonValue* req_obj = params->find("request");
        const std::string req_url =
            (req_obj != nullptr && req_obj->is_object()) ? req_obj->get_string("url") : "";
        if (!req_id.empty() && cdp::should_track_network_request(rtype, req_url)) {
          inflight_reqs_.insert(req_id);
          last_net_activity_ms_ = steady_now_ms();
        }
      } else if ((method == "Network.loadingFinished" ||
                  method == "Network.loadingFailed" ||
                  method == "Network.requestServedFromCache") &&
                 params != nullptr) {
        const std::string req_id = params->get_string("requestId");
        if (!req_id.empty()) {
          inflight_reqs_.erase(req_id);
          last_net_activity_ms_ = steady_now_ms();
        }
      }
    } catch (...) {
    }
  }

  void teardown(bool from_destructor) noexcept {
    HINTERNET ws = nullptr;
    HINTERNET conn = nullptr;
    HINTERNET sess = nullptr;
    HANDLE proc = nullptr;
    std::string prof_dir;
    std::thread reader;

    try {
      {
        std::lock_guard<std::mutex> lock(mu_);
        stopping_ = true;
        connected_ = false;
        ws = h_ws_;
        conn = h_conn_;
        sess = h_session_;
        h_ws_ = nullptr;
        h_conn_ = nullptr;
        h_session_ = nullptr;
        proc = proc_handle_;
        proc_handle_ = nullptr;
        prof_dir = std::move(profile_dir_);
        profile_dir_.clear();
        if (reader_thread_.joinable()) {
          reader = std::move(reader_thread_);
        }
        cv_.notify_all();
      }

      if (ws != nullptr) {
        WinHttpWebSocketShutdown(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0U);
        WinHttpCloseHandle(ws);
      }
      if (conn != nullptr) {
        WinHttpCloseHandle(conn);
      }
      if (sess != nullptr) {
        WinHttpCloseHandle(sess);
      }

      if (reader.joinable()) {
        if (from_destructor) {
          reader.detach();
        } else {
          reader.join();
        }
      }

      if (proc != nullptr) {
        const DWORD wait_rc = WaitForSingleObject(proc, 400U);
        if (wait_rc == WAIT_TIMEOUT) {
          TerminateProcess(proc, 0U);
          WaitForSingleObject(proc, 1200U);
        }
        CloseHandle(proc);
      }

      if (!prof_dir.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::path(utf8_to_wide(prof_dir)), ec);
      }
    } catch (...) {
    }
  }

  std::mutex send_mu_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::atomic<std::int64_t> next_id_{1LL};
  std::unordered_map<std::int64_t, std::shared_ptr<PendingReply>> pending_;
  std::unordered_set<std::string> inflight_reqs_;
  std::int64_t dom_seq_ = 0;
  std::int64_t load_seq_ = 0;
  std::int64_t net_idle_seq_ = 0;
  std::int64_t net_almost_idle_seq_ = 0;
  std::int64_t last_net_activity_ms_ = 0;

  HINTERNET h_session_ = nullptr;
  HINTERNET h_conn_ = nullptr;
  HINTERNET h_ws_ = nullptr;
  HANDLE proc_handle_ = nullptr;
  std::string profile_dir_;
  std::thread reader_thread_;
  bool connected_ = false;
  bool stopping_ = false;
};

using CdpSessionXPtr = cpp11::external_pointer<CdpSession>;

CdpSession* unwrap_cdp_session(SEXP xptr_sexp) {
  if (xptr_sexp == R_NilValue || TYPEOF(xptr_sexp) != EXTPTRSXP) {
    cpp11::stop("Expected a valid winrpa_web_session external pointer.");
  }
  CdpSessionXPtr xptr(xptr_sexp);
  CdpSession* ptr = xptr.get();
  if (ptr == nullptr) {
    cpp11::stop("Web CDP session is already closed.");
  }
  return ptr;
}

std::string build_temp_profile_dir() {
  wchar_t temp_buf[MAX_PATH]{};
  DWORD len = GetTempPathW(MAX_PATH, temp_buf);
  std::wstring base = (len > 0U && len < MAX_PATH) ? std::wstring(temp_buf, len) : L".\\";
  static std::atomic<std::uint64_t> seq{0ULL};
  const std::uint64_t s = seq.fetch_add(1ULL, std::memory_order_relaxed);
  const std::wstring name =
      L"winrpa_cdp_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(s);
  const std::filesystem::path p = std::filesystem::path(base) / name;
  std::error_code ec;
  std::filesystem::create_directories(p, ec);
  const std::wstring full = p.wstring();
  return wide_to_utf8(full.c_str(), static_cast<int>(full.size()));
}

std::string infer_browser_tag(const std::string& exe_path) {
  std::string low = exe_path;
  for (char& c : low) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (low.find("msedge") != std::string::npos || low.find("edge") != std::string::npos) {
    return "edge";
  }
  if (low.find("chrome") != std::string::npos) {
    return "chrome";
  }
  return "chromium";
}

}  // namespace

void shutdown_all_cdp_sessions() {
  const auto all = SessionRegistry::instance().snapshot();
  for (CdpSession* s : all) {
    if (s != nullptr) {
      s->close();
    }
  }
}

}  // namespace winrpa

#else

namespace winrpa {
void shutdown_all_cdp_sessions() {}
}  // namespace winrpa

#endif  // _WIN32

[[cpp11::register]]
cpp11::writable::list cpp_cdp_json_selftest() {
  using namespace winrpa::cdp;
  const JsonValue doc = JsonValue::make_object({
      {"text", JsonValue::make_string("hello \"winrpa\"\\line\n")},
      {"unicode", JsonValue::make_string("caf\xC3\xA9")},
      {"count", JsonValue::make_int(42LL)},
      {"flag", JsonValue::make_bool(true)},
      {"items", JsonValue::make_array({JsonValue::make_int(1LL), JsonValue::make_double(2.5)})}
  });
  const std::string raw = dump_json(doc);
  const JsonValue parsed = parse_json(raw);
  const JsonValue parsed_esc = parse_json("{\"u\":\"caf\\u00e9\"}");

  bool invalid_threw = false;
  try {
    (void)parse_json("{\"unterminated\": [1, 2");
  } catch (const std::exception&) {
    invalid_threw = true;
  }

  using namespace cpp11::literals;
  return cpp11::writable::list({
      "ok"_nm = parsed.is_object(),
      "roundtrip_text"_nm = parsed.get_string("text"),
      "unicode_text"_nm = parsed_esc.get_string("u"),
      "count"_nm = static_cast<int>(parsed.get_int64("count")),
      "flag"_nm = parsed.get_bool("flag"),
      "invalid_threw"_nm = invalid_threw
  });
}

[[cpp11::register]]
cpp11::writable::raws cpp_cdp_base64_selftest(std::string b64) {
  const auto bytes = winrpa::cdp::base64_decode(b64);
  cpp11::writable::raws out(static_cast<R_xlen_t>(bytes.size()));
  for (std::size_t i = 0U; i < bytes.size(); ++i) {
    out[static_cast<R_xlen_t>(i)] = static_cast<Rbyte>(bytes[i]);
  }
  return out;
}

[[cpp11::register]]
cpp11::writable::list cpp_cdp_network_filter_selftest() {
  using winrpa::cdp::should_track_network_request;
  using namespace cpp11::literals;
  return cpp11::writable::list({
      "websocket"_nm = should_track_network_request("WebSocket", "wss://alive.github.com/ws"),
      "eventsource"_nm = should_track_network_request("EventSource", "https://example.org/events"),
      "ping"_nm = should_track_network_request("Ping", "https://api.github.com/_private/browser/stats"),
      "csp"_nm = should_track_network_request("CSPViolationReport", "https://example.org/csp"),
      "preflight"_nm = should_track_network_request("Preflight", "https://api.example.org"),
      "data_url"_nm = should_track_network_request("Image", "data:image/png;base64,AAAA"),
      "blob_url"_nm = should_track_network_request("Other", "blob:https://example.org/123"),
      "xhr"_nm = should_track_network_request("XHR", "https://github.com/foo/bar"),
      "fetch"_nm = should_track_network_request("Fetch", "https://github.com/foo/bar"),
      "document"_nm = should_track_network_request("Document", "https://github.com/foo/bar")
  });
}

[[cpp11::register]]
std::string cpp_cdp_find_browser(std::string preference) {
#ifdef _WIN32
  return winrpa::find_browser_executable(preference);
#else
  (void)preference;
  return "";
#endif
}

[[cpp11::register]]
cpp11::writable::list cpp_cdp_launch(
    std::string browser,
    bool headless,
    std::string url,
    int port,
    cpp11::strings extra_args,
    int timeout_ms) {
#ifdef _WIN32
  std::string exe_path = browser;
  std::error_code ec;
  if (exe_path.empty() || exe_path == "auto" || exe_path == "edge" || exe_path == "chrome") {
    exe_path = winrpa::find_browser_executable(exe_path);
  } else if (!std::filesystem::is_regular_file(
                 std::filesystem::path(winrpa::utf8_to_wide(exe_path)), ec)) {
    cpp11::stop("Browser executable does not exist: '%s'.", exe_path.c_str());
  }
  if (exe_path.empty()) {
    cpp11::stop("No supported Chromium browser (Microsoft Edge or Google Chrome) was found.");
  }

  const int dbg_port = port > 0 ? port : winrpa::pick_free_debug_port();
  const std::string prof_dir = winrpa::build_temp_profile_dir();

  std::wstring cmdline = winrpa::quote_cmd_arg(winrpa::utf8_to_wide(exe_path));
  cmdline += L" --remote-debugging-port=" + std::to_wstring(dbg_port);
  cmdline += L" --user-data-dir=" + winrpa::quote_cmd_arg(winrpa::utf8_to_wide(prof_dir));
  cmdline += L" --remote-allow-origins=*";
  cmdline += L" --no-first-run --no-default-browser-check";
  cmdline += L" --disable-background-networking --disable-sync";
  if (headless) {
    cmdline += L" --headless=new --disable-gpu";
  }
  for (R_xlen_t i = 0; i < extra_args.size(); ++i) {
    const std::string a = std::string(extra_args[i]);
    if (!a.empty()) {
      cmdline += L" " + winrpa::quote_cmd_arg(winrpa::utf8_to_wide(a));
    }
  }
  const std::string start_url = url.empty() ? "about:blank" : url;
  cmdline += L" " + winrpa::quote_cmd_arg(winrpa::utf8_to_wide(start_url));

  std::vector<wchar_t> cmd_buf(cmdline.begin(), cmdline.end());
  cmd_buf.push_back(L'\0');

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  if (!headless) {
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
  }
  PROCESS_INFORMATION pi{};
  const DWORD flags = headless ? CREATE_NO_WINDOW : 0U;

  if (!CreateProcessW(
          nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, flags,
          nullptr, nullptr, &si, &pi)) {
    const DWORD err = GetLastError();
    std::filesystem::remove_all(std::filesystem::path(winrpa::utf8_to_wide(prof_dir)), ec);
    cpp11::stop("CreateProcessW failed launching browser (Win32 error %lu).", err);
  }
  if (pi.hThread != nullptr) {
    CloseHandle(pi.hThread);
  }

  std::unique_ptr<winrpa::CdpSession> sess(new winrpa::CdpSession());
  sess->attach_browser_process(pi.hProcess, prof_dir);

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 15000);
  std::string ws_url;
  std::string last_err = "Timed out waiting for browser CDP endpoint.";
  while (std::chrono::steady_clock::now() < deadline) {
    winrpa::check_abort_or_interrupt();
    ws_url = winrpa::find_page_ws_url("127.0.0.1", dbg_port, 1, true, last_err);
    if (!ws_url.empty()) {
      break;
    }
    Sleep(75U);
  }
  if (ws_url.empty()) {
    cpp11::stop("%s", last_err.c_str());
  }

  try {
    sess->open_websocket(ws_url);
  } catch (const std::exception& e) {
    cpp11::stop("%s", e.what());
  }

  const int child_pid = static_cast<int>(pi.dwProcessId);
  const std::string tag = winrpa::infer_browser_tag(exe_path);
  winrpa::CdpSessionXPtr xptr(sess.release());

  using namespace cpp11::literals;
  return cpp11::writable::list({
      "ptr"_nm = xptr,
      "host"_nm = std::string("127.0.0.1"),
      "port"_nm = dbg_port,
      "pid"_nm = child_pid,
      "browser"_nm = tag,
      "headless"_nm = headless,
      "ws_url"_nm = ws_url,
      "profile_dir"_nm = prof_dir
  });
#else
  (void)browser;
  (void)headless;
  (void)url;
  (void)port;
  (void)extra_args;
  (void)timeout_ms;
  winrpa::require_windows_or_stop("cpp_cdp_launch()");
  return cpp11::writable::list();
#endif
}

[[cpp11::register]]
cpp11::writable::list cpp_cdp_connect(
    std::string host,
    int port,
    int target_index,
    int timeout_ms) {
#ifdef _WIN32
  std::string err;
  std::string ws_url;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 8000);
  while (std::chrono::steady_clock::now() < deadline) {
    winrpa::check_abort_or_interrupt();
    ws_url = winrpa::find_page_ws_url(host, port, target_index, false, err);
    if (!ws_url.empty()) {
      break;
    }
    Sleep(75U);
  }
  if (ws_url.empty()) {
    cpp11::stop("%s", err.c_str());
  }

  std::unique_ptr<winrpa::CdpSession> sess(new winrpa::CdpSession());
  try {
    sess->open_websocket(ws_url);
  } catch (const std::exception& e) {
    cpp11::stop("%s", e.what());
  }

  winrpa::CdpSessionXPtr xptr(sess.release());
  using namespace cpp11::literals;
  return cpp11::writable::list({
      "ptr"_nm = xptr,
      "host"_nm = host,
      "port"_nm = port,
      "pid"_nm = 0,
      "browser"_nm = std::string("chromium"),
      "headless"_nm = false,
      "ws_url"_nm = ws_url,
      "profile_dir"_nm = std::string("")
  });
#else
  (void)host;
  (void)port;
  (void)target_index;
  (void)timeout_ms;
  winrpa::require_windows_or_stop("cpp_cdp_connect()");
  return cpp11::writable::list();
#endif
}

[[cpp11::register]]
void cpp_cdp_close(SEXP sess_xptr) {
#ifdef _WIN32
  if (sess_xptr == R_NilValue || TYPEOF(sess_xptr) != EXTPTRSXP) {
    return;
  }
  winrpa::CdpSessionXPtr xptr(sess_xptr);
  winrpa::CdpSession* s = xptr.get();
  if (s != nullptr) {
    s->close();
  }
#else
  (void)sess_xptr;
#endif
}

[[cpp11::register]]
std::string cpp_cdp_send(
    SEXP sess_xptr,
    std::string method,
    std::string params_json,
    int timeout_ms) {
#ifdef _WIN32
  winrpa::CdpSession* s = winrpa::unwrap_cdp_session(sess_xptr);
  try {
    return s->send_command(method, params_json, timeout_ms);
  } catch (const std::exception& e) {
    cpp11::stop("%s", e.what());
  }
  return "{}";
#else
  (void)sess_xptr;
  (void)method;
  (void)params_json;
  (void)timeout_ms;
  winrpa::require_windows_or_stop("cpp_cdp_send()");
  return "{}";
#endif
}

[[cpp11::register]]
cpp11::writable::list cpp_cdp_event_state(SEXP sess_xptr) {
#ifdef _WIN32
  winrpa::CdpSession* s = winrpa::unwrap_cdp_session(sess_xptr);
  const auto st = s->event_state();
  using namespace cpp11::literals;
  return cpp11::writable::list({
      "dom_seq"_nm = static_cast<double>(st.dom_seq),
      "load_seq"_nm = static_cast<double>(st.load_seq),
      "net_idle_seq"_nm = static_cast<double>(st.net_idle_seq),
      "net_almost_idle_seq"_nm = static_cast<double>(st.net_almost_idle_seq),
      "in_flight"_nm = st.in_flight,
      "ms_since_net_activity"_nm = st.ms_since_net,
      "connected"_nm = st.connected
  });
#else
  (void)sess_xptr;
  winrpa::require_windows_or_stop("cpp_cdp_event_state()");
  return cpp11::writable::list();
#endif
}

[[cpp11::register]]
std::string cpp_cdp_write_base64_file(std::string b64, std::string out_path) {
  std::vector<std::uint8_t> bytes;
  try {
    bytes = winrpa::cdp::base64_decode(b64);
  } catch (const std::exception& e) {
    cpp11::stop("Failed to decode Base64 payload: %s", e.what());
  }
#ifdef _WIN32
  const std::filesystem::path fpath(winrpa::utf8_to_wide(out_path));
#else
  const std::filesystem::path fpath(out_path);
#endif
  std::ofstream ofs(fpath, std::ios::binary | std::ios::trunc);
  if (!ofs.is_open()) {
    cpp11::stop("Failed to open output file for writing: '%s'.", out_path.c_str());
  }
  if (!bytes.empty()) {
    ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!ofs.good()) {
    cpp11::stop("Failed writing decoded binary bytes to '%s'.", out_path.c_str());
  }
  return out_path;
}

namespace winrpa {
namespace {

SEXP json_value_to_sexp(const winrpa::cdp::JsonValue& v) {
  using T = winrpa::cdp::JsonValue::Type;
  switch (v.type) {
    case T::kNull:
      return R_NilValue;
    case T::kBool:
      return cpp11::as_sexp(v.bool_val);
    case T::kNumber:
      if (v.is_int && v.int_val >= -2147483647LL && v.int_val <= 2147483647LL) {
        return cpp11::as_sexp(static_cast<int>(v.int_val));
      }
      return cpp11::as_sexp(v.num_val);
    case T::kString:
      return cpp11::as_sexp(v.str_val);
    case T::kArray: {
      cpp11::writable::list out(static_cast<R_xlen_t>(v.arr_val.size()));
      for (std::size_t i = 0U; i < v.arr_val.size(); ++i) {
        out[static_cast<R_xlen_t>(i)] = json_value_to_sexp(v.arr_val[i]);
      }
      return out;
    }
    case T::kObject: {
      const R_xlen_t n = static_cast<R_xlen_t>(v.obj_val.size());
      cpp11::writable::list out(n);
      cpp11::writable::strings nms(n);
      for (std::size_t i = 0U; i < v.obj_val.size(); ++i) {
        nms[static_cast<R_xlen_t>(i)] = v.obj_val[i].first;
        out[static_cast<R_xlen_t>(i)] = json_value_to_sexp(v.obj_val[i].second);
      }
      out.attr("names") = nms;
      return out;
    }
  }
  return R_NilValue;
}

}  // namespace
}  // namespace winrpa

[[cpp11::register]]
SEXP cpp_cdp_parse_json(std::string raw_json) {
  try {
    const auto parsed = winrpa::cdp::parse_json(raw_json);
    return winrpa::json_value_to_sexp(parsed);
  } catch (const std::exception& e) {
    cpp11::stop("Invalid JSON response: %s", e.what());
  }
  return R_NilValue;
}
