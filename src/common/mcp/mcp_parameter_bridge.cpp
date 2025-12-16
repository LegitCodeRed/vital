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

#include "mcp_parameter_bridge.h"
#include "synth_base.h"
#include "json/json.h"

namespace vital {

McpParameterBridge::McpParameterBridge(SynthBase* synth_base)
    : synth_base_(synth_base),
      mcp_manager_(McpServerManager::getInstance()),
      is_running_(false),
      metadata_sent_(false) {
}

McpParameterBridge::~McpParameterBridge() {
  stop();
}

void McpParameterBridge::start() {
  if (is_running_) {
    return;
  }

  is_running_ = true;
  metadata_sent_ = false;

  // Start polling timer
  startTimer(kPollIntervalMs);

  // Send all parameter metadata to MCP server
  sendAllParameterMetadata();
}

void McpParameterBridge::stop() {
  if (!is_running_) {
    return;
  }

  is_running_ = false;
  stopTimer();

  const ScopedLock lock(pending_changes_lock_);
  pending_changes_.clear();
}

void McpParameterBridge::onParameterChanged(const std::string& name, mono_float value) {
  if (!is_running_ || !mcp_manager_ || !mcp_manager_->isServerRunning()) {
    return;
  }

  // Check if this is an echo of our own change
  {
    const ScopedLock lock(pending_changes_lock_);
    if (pending_changes_.count(name) > 0) {
      // This is an echo, remove from pending and don't notify
      pending_changes_.erase(name);
      return;
    }
  }

  // Send parameter change notification to MCP server
  String json_body = "{\"name\":\"" + String(name.c_str()) +
                     "\",\"value\":" + String(value, 6) + "}";

  // Send asynchronously via POST to /api/notify/parameter_changed
  mcp_manager_->httpPost("/api/notify/parameter_changed", json_body);
}

void McpParameterBridge::sendAllParameterMetadata() {
  if (!is_running_ || !mcp_manager_ || !mcp_manager_->isServerRunning()) {
    return;
  }

  // Build array of all parameter metadata
  String json_array = "[";
  bool first = true;

  int num_params = parameter_lookup_.getNumParameters();
  for (int i = 0; i < num_params; ++i) {
    const ValueDetails* details = parameter_lookup_.getDetails(i);
    if (details) {
      // Get current value from synth
      mono_float current_value = 0.0f;
      auto& controls = synth_base_->getControls();
      auto control_it = controls.find(details->name);
      if (control_it != controls.end()) {
        current_value = control_it->second->value();
      }

      if (!first) {
        json_array += ",";
      }
      first = false;

      json_array += parameterDetailsToJson(details->name, *details, current_value);
    }
  }

  json_array += "]";

  // Send to MCP server via POST to /api/metadata/parameters
  String response = mcp_manager_->httpPost("/api/metadata/parameters",
                                           "{\"parameters\":" + json_array + "}");

  if (response.isNotEmpty()) {
    metadata_sent_ = true;
  }
}

void McpParameterBridge::timerCallback() {
  if (!is_running_ || !mcp_manager_ || !mcp_manager_->isServerRunning()) {
    return;
  }

  // Send metadata if not sent yet
  if (!metadata_sent_) {
    sendAllParameterMetadata();
  }

  // Poll for parameter change requests from MCP server
  pollParameterChanges();
}

void McpParameterBridge::pollParameterChanges() {
  // GET pending parameter changes from MCP server
  String response = mcp_manager_->httpGet("/api/pending_changes");

  if (response.isEmpty()) {
    return;
  }

  // Parse JSON response
  try {
    auto json_response = nlohmann::json::parse(response.toStdString());

    if (json_response.count("parameters") > 0 && json_response["parameters"].is_array()) {
      for (const auto& param : json_response["parameters"]) {
        if (param.count("name") > 0 && param.count("value") > 0) {
          std::string name = param["name"].get<std::string>();
          double value = param["value"].get<double>();

          applyParameterChange(name, static_cast<mono_float>(value));
        }
      }
    }
  } catch (const nlohmann::json::exception& e) {
    DBG("Failed to parse parameter changes JSON: " << e.what());
  }
}

void McpParameterBridge::applyParameterChange(const std::string& name, mono_float value) {
  // Validate parameter exists
  if (!parameter_lookup_.isParameter(name)) {
    DBG("Unknown parameter from MCP: " << name.c_str());
    return;
  }

  // Get parameter details for range validation
  const ValueDetails& details = parameter_lookup_.getDetails(name);

  // Clamp value to valid range
  mono_float clamped_value = jlimit(details.min, details.max, value);

  // Mark as pending to prevent echo
  {
    const ScopedLock lock(pending_changes_lock_);
    pending_changes_.insert(name);
  }

  // Apply change to Vital via SynthBase
  synth_base_->valueChangedExternal(name, clamped_value);
}

String McpParameterBridge::parameterDetailsToJson(const std::string& name,
                                                   const ValueDetails& details,
                                                   mono_float current_value) {
  String json = "{";

  json += "\"name\":\"" + String(name.c_str()) + "\",";
  json += "\"display_name\":\"" + String(details.display_name.c_str()) + "\",";
  json += "\"value\":" + String(current_value, 6) + ",";
  json += "\"min\":" + String(details.min, 6) + ",";
  json += "\"max\":" + String(details.max, 6) + ",";
  json += "\"default\":" + String(details.default_value, 6) + ",";
  json += "\"is_automation_parameter\":true";

  // Add optional fields
  if (!details.display_units.empty()) {
    json += ",\"units\":\"" + String(details.display_units.c_str()) + "\"";
  }

  if (details.string_lookup != nullptr) {
    json += ",\"string_lookup\":[";
    // Count strings in lookup
    int count = 0;
    while (!details.string_lookup[count].empty()) {
      if (count > 0) json += ",";
      json += "\"" + String(details.string_lookup[count].c_str()) + "\"";
      count++;
    }
    json += "]";
  }

  if (details.version_added > 0) {
    json += ",\"version_added\":" + String(details.version_added);
  }

  json += "}";

  return json;
}

} // namespace vital
