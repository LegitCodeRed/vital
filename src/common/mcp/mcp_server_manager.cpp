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
juce_ImplementSingleton(McpServerManager)

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

McpServerManager* McpServerManager::getInstance() {
  return McpServerManager::getInstanceWithoutCreating();
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
    // Try graceful shutdown first
    McpMessage shutdown = McpMessage::createNotification("shutdown", nlohmann::json());
    String json_str = String(shutdown.toJson().c_str()) + "\n";
    server_process_->writeToStdin(json_str.toUTF8(), json_str.getNumBytesAsUTF8());

    // Wait 1 second for graceful shutdown
    Thread::sleep(1000);

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
  if (!server_process_) {
    return;
  }

  char buffer[4096];
  int bytes_read = server_process_->readProcessOutput(buffer, sizeof(buffer));

  if (bytes_read > 0) {
    stdout_buffer_.write(buffer, bytes_read);

    // Parse complete lines
    String output = stdout_buffer_.toString();
    parseMessages(output);
  }
}

void McpServerManager::processStdin() {
  if (!server_process_) {
    return;
  }

  // Send up to 10 messages per timer tick to avoid blocking
  for (int i = 0; i < 10; ++i) {
    McpMessage msg;
    if (!outgoing_queue_.try_dequeue(msg)) {
      break;
    }

    String json_str = String(msg.toJson().c_str()) + "\n";
    server_process_->writeToStdin(json_str.toUTF8(), json_str.getNumBytesAsUTF8());
  }
}

void McpServerManager::parseMessages(const String& output) {
  StringArray lines;
  lines.addTokens(incomplete_line_ + output, "\n", "");

  // Process all complete lines except the last (which might be incomplete)
  for (int i = 0; i < lines.size() - 1; ++i) {
    String line = lines[i].trim();
    if (line.isEmpty()) {
      continue;
    }

    try {
      McpMessage msg = McpMessage::fromJson(line.toStdString());

      // Handle special messages
      if (msg.method == "server_ready") {
        setStatus(McpServerStatus::Running);
        restart_attempts_ = 0;  // Reset on successful start
      } else if (msg.method == "pong" && msg.is_response) {
        // Heartbeat response
        last_heartbeat_ = Time::getCurrentTime();
        pending_heartbeat_id_.clear();
      } else {
        // Enqueue for processing
        incoming_queue_.enqueue(msg);
      }
    } catch (...) {
      // Invalid JSON, skip
    }
  }

  // Save incomplete line for next iteration
  if (!lines.isEmpty()) {
    incomplete_line_ = lines[lines.size() - 1];
  }

  // Clear buffer
  stdout_buffer_.reset();
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
  pending_heartbeat_id_ = "hb-" + String(Time::getCurrentTime().toMilliseconds());
  McpMessage ping = McpMessage::createRequest("ping", nlohmann::json(), pending_heartbeat_id_.toStdString());
  sendMessage(ping);
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

} // namespace vital
