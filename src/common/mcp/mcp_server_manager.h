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

#pragma once

#include "JuceHeader.h"
#include "mcp_types.h"
#include "concurrentqueue/concurrentqueue.h"
#include <memory>
#include <atomic>

namespace vital {

/**
 * Manages the lifecycle of the Node.js MCP server subprocess.
 * Handles stdio communication, process monitoring, crash detection, and auto-restart.
 *
 * This is a singleton class - use getInstance() to access it.
 * Thread-safe for message passing via lock-free queues.
 */
class McpServerManager : public Timer, public DeletedAtShutdown {
public:
  /**
   * Lifecycle management
   */
  bool startServer();
  void stopServer();
  bool isServerRunning() const;
  McpServerStatus getStatus() const { return status_; }
  String getLastError() const;

  /**
   * Configuration
   */
  void setPort(int port);
  int getPort() const { return port_; }
  void setAutoStart(bool auto_start);
  bool shouldAutoStart() const { return auto_start_; }
  void setNodeExecutablePath(const String& path);
  String getNodeExecutablePath() const;

  /**
   * Thread-safe messaging
   */
  void sendMessage(const McpMessage& msg);
  bool hasIncomingMessages() const;
  McpMessage getNextMessage();

  /**
   * Listener interface for status changes
   */
  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void mcpServerStatusChanged(McpServerStatus new_status, const String& error_message) = 0;
  };

  void addListener(Listener* listener);
  void removeListener(Listener* listener);

  /**
   * Statistics and diagnostics
   */
  int getMessagesSent() const { return messages_sent_.load(); }
  int getMessagesReceived() const { return messages_received_.load(); }
  Time getLastHeartbeatTime() const { return last_heartbeat_; }
  int getRestartCount() const { return restart_count_; }

  /**
   * HTTP helpers (public for use by McpParameterBridge)
   */
  String httpPost(const String& endpoint, const String& json_body);
  String httpGet(const String& endpoint);

  // Singleton declaration (must be public to access getInstance())
  JUCE_DECLARE_SINGLETON(McpServerManager, false)

private:
  McpServerManager();
  ~McpServerManager() override;

  // Timer callback for polling stdout and health monitoring
  void timerCallback() override;

  // Process management
  bool spawnProcess();
  void killProcess();
  void handleCrash();
  int calculateRestartDelay() const;

  // stdio communication
  void processStdout();
  void processStdin();
  void parseMessages(const String& output);

  // Node.js executable detection
  String findNodeExecutable();
  String getServerScriptPath();

  // Heartbeat
  void sendHeartbeat();
  void handleHeartbeatTimeout();

  // Status updates
  void setStatus(McpServerStatus new_status, const String& error = String());
  void notifyListeners();

  // Process and communication
  std::unique_ptr<ChildProcess> server_process_;
  MemoryOutputStream stdout_buffer_;
  String incomplete_line_;  // Buffer for incomplete JSON lines

  // Lock-free message queues
  moodycamel::ConcurrentQueue<McpMessage> incoming_queue_;
  moodycamel::ConcurrentQueue<McpMessage> outgoing_queue_;

  // Configuration
  CriticalSection config_lock_;
  int port_;
  bool auto_start_;
  String node_path_;
  File server_script_path_;

  // Status and diagnostics
  std::atomic<McpServerStatus> status_;
  String last_error_;
  Time last_heartbeat_;
  Time last_heartbeat_sent_;
  Time startup_time_;
  String pending_heartbeat_id_;
  int restart_count_;
  int restart_attempts_;
  std::atomic<int> messages_sent_;
  std::atomic<int> messages_received_;

  // Listeners
  ListenerList<Listener> listeners_;

  // Constants
  static constexpr int kTimerIntervalMs = 50;          // Poll stdout every 50ms
  static constexpr int kHeartbeatIntervalMs = 10000;   // Send heartbeat every 10s
  static constexpr int kHeartbeatTimeoutMs = 30000;    // Timeout after 30s
  static constexpr int kStartupTimeoutMs = 5000;       // Max 5s to start
  static constexpr int kMaxRestartDelay = 30;          // Max 30s restart delay
  static constexpr int kBaseRestartDelay = 1;          // Start with 1s delay

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(McpServerManager)
};

} // namespace vital
