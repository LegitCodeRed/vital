/* Copyright 2013-2019 Matt Tytel
 *
 * vital is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vital is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with vital.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "mcp_server_manager.h"
#include <sstream>

namespace vital {

// Singleton instance
JUCE_IMPLEMENT_SINGLETON(McpServerManager)

McpServerManager::McpServerManager()
    : port_(3000),
      auto_start_(false),
      status_(McpServerStatus::Stopped),
      restart_count_(0),
      restart_attempts_(0),
      messages_sent_(0),
      messages_received_(0) {

  // Find Node.js executable on startup
  node_path_ = findNodeExecutable();

  // Set server script path relative to this file
  server_script_path_ = File::getSpecialLocation(File::currentExecutableFile)
                            .getParentDirectory()
                            .getChildFile("mcp_server")
                            .getChildFile("build")
                            .getChildFile("index.js");
}

McpServerManager::~McpServerManager() {
  stopServer();
  clearSingletonInstance();
}

// ============================================================================
// Lifecycle Management
// ============================================================================

bool McpServerManager::startServer() {
  if (isServerRunning()) {
    return true;
  }

  if (node_path_.isEmpty()) {
    setStatus(McpServerStatus::Error, "Node.js executable not found. Please install Node.js or set path manually.");
    return false;
  }

  if (!server_script_path_.existsAsFile()) {
    setStatus(McpServerStatus::Error, "MCP server script not found at: " + server_script_path_.getFullPathName());
    return false;
  }

  setStatus(McpServerStatus::Starting);
  restart_attempts_ = 0;

  if (spawnProcess()) {
    startTimer(kTimerIntervalMs);
    return true;
  }

  setStatus(McpServerStatus::Error, "Failed to spawn Node.js process");
  return false;
}

void McpServerManager::stopServer() {
  if (!isServerRunning()) {
    return;
  }

  stopTimer();
  killProcess();
  setStatus(McpServerStatus::Stopped);

  // Clear message queues
  McpMessage msg;
  while (incoming_queue_.try_dequeue(msg)) {}
  while (outgoing_queue_.try_dequeue(msg)) {}
}

bool McpServerManager::isServerRunning() const {
  return server_process_ != nullptr && server_process_->isRunning();
}

String McpServerManager::getLastError() const {
  const ScopedLock lock(config_lock_);
  return last_error_;
}

// ============================================================================
// Configuration
// ============================================================================

void McpServerManager::setPort(int port) {
  const ScopedLock lock(config_lock_);
  port_ = port;
}

void McpServerManager::setAutoStart(bool auto_start) {
  const ScopedLock lock(config_lock_);
  auto_start_ = auto_start;
}

void McpServerManager::setNodeExecutablePath(const String& path) {
  const ScopedLock lock(config_lock_);
  node_path_ = path;
}

String McpServerManager::getNodeExecutablePath() const {
  const ScopedLock lock(config_lock_);
  return node_path_;
}

// ============================================================================
// Thread-safe Messaging
// ============================================================================

void McpServerManager::sendMessage(const McpMessage& msg) {
  outgoing_queue_.enqueue(msg);
  messages_sent_.fetch_add(1);
}

bool McpServerManager::hasIncomingMessages() const {
  return incoming_queue_.size_approx() > 0;
}

McpMessage McpServerManager::getNextMessage() {
  McpMessage msg;
  if (incoming_queue_.try_dequeue(msg)) {
    messages_received_.fetch_add(1);
    return msg;
  }
  return msg;
}

// ============================================================================
// Listener Management
// ============================================================================

void McpServerManager::addListener(Listener* listener) {
  listeners_.add(listener);
}

void McpServerManager::removeListener(Listener* listener) {
  listeners_.remove(listener);
}

// ============================================================================
// Timer Callback (Main polling loop)
// ============================================================================

void McpServerManager::timerCallback() {
  // Check if process crashed
  if (!isServerRunning()) {
    handleCrash();
    return;
  }

  // Process stdout (read incoming messages)
  processStdout();

  // Process stdin (send outgoing messages)
  processStdin();

  // Check heartbeat timeout
  if (status_ == McpServerStatus::Running) {
    auto now = Time::getCurrentTime();

    // Send heartbeat if interval elapsed
    if ((now - last_heartbeat_sent_).inMilliseconds() >= kHeartbeatIntervalMs) {
      sendHeartbeat();
    }

    // Check for timeout
    if (!pending_heartbeat_id_.isEmpty() &&
        (now - last_heartbeat_sent_).inMilliseconds() >= kHeartbeatTimeoutMs) {
      handleHeartbeatTimeout();
    }
  }
}

// ============================================================================
// Process Management
// ============================================================================

bool McpServerManager::spawnProcess() {
  server_process_ = std::make_unique<ChildProcess>();

  // Build command line
  String command_line = node_path_.quoted();
  command_line += " " + server_script_path_.getFullPathName().quoted();
  command_line += " --port=" + String(port_);

  // Spawn process with redirected stdout/stderr
  if (!server_process_->start(command_line, ChildProcess::wantStdOut | ChildProcess::wantStdErr)) {
    server_process_.reset();
    return false;
  }

  stdout_buffer_.reset();
  incomplete_line_.clear();
  last_heartbeat_ = Time::getCurrentTime();
  last_heartbeat_sent_ = Time::getCurrentTime();
  pending_heartbeat_id_.clear();

  return true;
}

void McpServerManager::killProcess() {
  if (server_process_) {
    // TODO: Implement graceful shutdown via ChildProcessMaster
    // For now, just kill the process
    if (server_process_->isRunning()) {
      server_process_->kill();
    }

    server_process_.reset();
  }
}

void McpServerManager::handleCrash() {
  if (status_ == McpServerStatus::Stopped) {
    return;  // Already stopped intentionally
  }

  restart_attempts_++;
  restart_count_++;

  int delay = calculateRestartDelay();

  setStatus(McpServerStatus::Error,
            "MCP server crashed. Restarting in " + String(delay) + " seconds... (attempt " +
            String(restart_attempts_) + ")");

  // Schedule restart
  Timer::callAfterDelay(delay * 1000, [this]() {
    if (status_ != McpServerStatus::Stopped) {
      if (spawnProcess()) {
        setStatus(McpServerStatus::Starting);
      } else {
        setStatus(McpServerStatus::Error, "Failed to restart MCP server");
      }
    }
  });
}

int McpServerManager::calculateRestartDelay() const {
  // Exponential backoff: 1s, 2s, 4s, 8s, 16s, max 30s
  int delay = kBaseRestartDelay * (1 << (restart_attempts_ - 1));
  return jmin(delay, kMaxRestartDelay);
}

// ============================================================================
// stdio Communication
// ============================================================================

void McpServerManager::processStdout() {
  // No longer needed with HTTP communication
  // Keep reading to prevent buffer overflow, but don't parse
  if (server_process_) {
    char buffer[4096];
    server_process_->readProcessOutput(buffer, sizeof(buffer));
  }
}

void McpServerManager::processStdin() {
  if (!server_process_) {
    return;
  }

  // Send outgoing messages via HTTP POST (synchronous for simplicity)
  for (int i = 0; i < 10; ++i) {
    McpMessage msg;
    if (!outgoing_queue_.try_dequeue(msg)) {
      break;
    }

    // Build JSON body and send via HTTP
    String json_body = String(msg.toJson().c_str());
    httpPost("/api/rpc", json_body);
  }
}

void McpServerManager::parseMessages(const String& output) {
  // No longer used with HTTP communication
}

// ============================================================================
// Node.js Executable Detection
// ============================================================================

String McpServerManager::findNodeExecutable() {
  // Try common Node.js installation paths
  StringArray possible_paths;

#if JUCE_WINDOWS
  possible_paths.add("C:\\Program Files\\nodejs\\node.exe");
  possible_paths.add("C:\\Program Files (x86)\\nodejs\\node.exe");

  // Check PATH environment variable
  String path_env = SystemStats::getEnvironmentVariable("PATH", "");
  StringArray path_dirs;
  path_dirs.addTokens(path_env, ";", "");

  for (const auto& dir : path_dirs) {
    possible_paths.add(File(dir).getChildFile("node.exe").getFullPathName());
  }
#elif JUCE_MAC
  possible_paths.add("/usr/local/bin/node");
  possible_paths.add("/opt/homebrew/bin/node");
  possible_paths.add("/usr/bin/node");
#elif JUCE_LINUX
  possible_paths.add("/usr/bin/node");
  possible_paths.add("/usr/local/bin/node");
  possible_paths.add("/opt/node/bin/node");
#endif

  // Check each path
  for (const auto& path : possible_paths) {
    File node_file(path);
    if (node_file.existsAsFile()) {
      return node_file.getFullPathName();
    }
  }

  return String();
}

String McpServerManager::getServerScriptPath() {
  return server_script_path_.getFullPathName();
}

// ============================================================================
// Heartbeat
// ============================================================================

void McpServerManager::sendHeartbeat() {
  // Send HTTP heartbeat to check if server is alive
  String response = httpPost("/api/heartbeat", "{}");

  if (response.isNotEmpty()) {
    last_heartbeat_ = Time::getCurrentTime();
    pending_heartbeat_id_.clear();

    // If we were starting, mark as running now
    if (status_.load() == McpServerStatus::Starting) {
      setStatus(McpServerStatus::Running);
      restart_attempts_ = 0;
    }
  }

  last_heartbeat_sent_ = Time::getCurrentTime();
}

void McpServerManager::handleHeartbeatTimeout() {
  setStatus(McpServerStatus::Error, "MCP server heartbeat timeout");
  killProcess();
  handleCrash();
}

// ============================================================================
// Status Updates
// ============================================================================

void McpServerManager::setStatus(McpServerStatus new_status, const String& error) {
  bool status_changed = (status_.load() != new_status);
  status_.store(new_status);

  {
    const ScopedLock lock(config_lock_);
    last_error_ = error;
  }

  if (status_changed) {
    notifyListeners();
  }
}

void McpServerManager::notifyListeners() {
  listeners_.call([this](Listener& l) {
    l.mcpServerStatusChanged(status_.load(), getLastError());
  });
}

// ============================================================================
// HTTP Helpers
// ============================================================================

String McpServerManager::httpPost(const String& endpoint, const String& json_body) {
  String url_str = "http://localhost:" + String(port_) + endpoint;
  URL url = url_str;

  // Add POST data to URL
  url = url.withPOSTData(json_body);

  // Create input stream with JUCE 5 API
  std::unique_ptr<InputStream> stream = url.createInputStream(
    true,  // doPostLikeRequest
    nullptr,  // progressCallback
    nullptr,  // progressCallbackContext
    "Content-Type: application/json\r\n",  // extraHeaders
    5000,  // connectionTimeOutMs
    nullptr,  // responseHeaders
    nullptr,  // statusCode
    0,  // numRedirectsToFollow
    "POST"  // httpRequestCmd
  );

  if (stream != nullptr) {
    return stream->readEntireStreamAsString();
  }

  return String();
}

String McpServerManager::httpGet(const String& endpoint) {
  String url_str = "http://localhost:" + String(port_) + endpoint;
  URL url = url_str;

  // Create input stream with JUCE 5 API
  std::unique_ptr<InputStream> stream = url.createInputStream(
    false,  // doPostLikeRequest
    nullptr,  // progressCallback
    nullptr,  // progressCallbackContext
    String(),  // extraHeaders
    5000,  // connectionTimeOutMs
    nullptr,  // responseHeaders
    nullptr,  // statusCode
    0,  // numRedirectsToFollow
    "GET"  // httpRequestCmd
  );

  if (stream != nullptr) {
    return stream->readEntireStreamAsString();
  }

  return String();
}

} // namespace vital
