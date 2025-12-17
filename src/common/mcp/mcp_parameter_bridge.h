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
#include "mcp_server_manager.h"
#include "synth_parameters.h"
#include "json/json.h"
#include <set>

class SynthBase;

namespace vital {

/**
 * Bridges Vital parameters to the MCP server.
 *
 * This class:
 * - Polls the MCP server for parameter change requests
 * - Applies changes to Vital via SynthBase
 * - Sends parameter change notifications to the MCP server
 * - Provides parameter metadata to the MCP server
 *
 * Thread-safe with echo prevention to avoid feedback loops.
 */
class McpParameterBridge : public Timer {
public:
  McpParameterBridge(SynthBase* synth_base);
  ~McpParameterBridge() override;

  /**
   * Start/stop the bridge
   */
  void start();
  void stop();
  bool isRunning() const { return is_running_; }

  /**
   * Called by SynthBase when a parameter changes
   * This sends the change to the MCP server
   */
  void onParameterChanged(const std::string& name, mono_float value);

  /**
   * Send all parameter metadata to MCP server
   */
  void sendAllParameterMetadata();

private:
  // Timer callback - polls MCP server for parameter changes
  void timerCallback() override;

  // Poll MCP server for pending parameter change requests
  void pollParameterChanges();

  // Apply a parameter change from MCP server
  void applyParameterChange(const std::string& name, mono_float value);

  // Send parameter metadata to MCP server
  void sendParameterMetadata(const std::string& name, const ValueDetails& details);

  // Convert ValueDetails to JSON object
  nlohmann::json parameterDetailsToJsonObject(const std::string& name, const ValueDetails& details, mono_float current_value);

  SynthBase* synth_base_;
  McpServerManager* mcp_manager_;
  ValueDetailsLookup parameter_lookup_;

  // Echo prevention: track parameters we're currently changing
  std::set<std::string> pending_changes_;
  CriticalSection pending_changes_lock_;

  bool is_running_;
  bool metadata_sent_;

  // Poll interval
  static constexpr int kPollIntervalMs = 1000;  // Poll every 1 second (async now, so can be less frequent)

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(McpParameterBridge)
};

} // namespace vital
