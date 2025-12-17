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

  // TEMP: Disable timer to isolate freeze issue
  // Start polling timer
  // startTimer(kPollIntervalMs);

  // TODO: Re-enable metadata sending after fixing corrupted string issue
  // For now, skip sending metadata on startup to prevent crash
  // sendAllParameterMetadata();
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

  // Send parameter change notification to MCP server via HTTP (async to avoid blocking)
  nlohmann::json body = {
    {"name", name},
    {"value", static_cast<double>(value)}
  };

  std::string json_str = body.dump();
  String json_body(json_str.c_str());

  auto manager = mcp_manager_;
  Thread::launch([manager, json_body]() {
    manager->httpPost("/api/notify/parameter_changed", json_body);
  });
}

void McpParameterBridge::sendAllParameterMetadata() {
  if (!is_running_ || !mcp_manager_ || !mcp_manager_->isServerRunning()) {
    return;
  }

  // Build array of all parameter metadata
  nlohmann::json parameters_array = nlohmann::json::array();

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

      parameters_array.push_back(parameterDetailsToJsonObject(details->name, *details, current_value));
    }
  }

  // Send to MCP server via HTTP
  nlohmann::json body = {
    {"parameters", parameters_array}
  };

  std::string json_str = body.dump();
  String json_body(json_str.c_str());
  String response = mcp_manager_->httpPost("/api/metadata/parameters", json_body);

  if (response.isNotEmpty()) {
    metadata_sent_ = true;
  }
}

void McpParameterBridge::timerCallback() {
  if (!is_running_ || !mcp_manager_ || !mcp_manager_->isServerRunning()) {
    return;
  }

  // TODO: Re-enable metadata sending after fixing corrupted string issue
  // Send metadata if not sent yet
  // if (!metadata_sent_) {
  //   sendAllParameterMetadata();
  // }

  // Poll for parameter change requests from MCP server
  pollParameterChanges();
}

void McpParameterBridge::pollParameterChanges() {
  // Poll async to avoid blocking the message thread
  auto manager = mcp_manager_;
  auto self = this;

  Thread::launch([manager, self]() {
    // GET pending parameter changes from MCP server
    String response = manager->httpGet("/api/pending_changes");

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

            self->applyParameterChange(name, static_cast<mono_float>(value));
          }
        }
      }
    } catch (const nlohmann::json::exception& e) {
      DBG("Failed to parse parameter changes JSON: " << e.what());
    }
  });
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

  // Apply change to Vital via SynthBase on message thread to avoid threading issues
  MessageManager::callAsync([this, name, clamped_value]() {
    synth_base_->valueChangedExternal(name, clamped_value);
  });
}

nlohmann::json McpParameterBridge::parameterDetailsToJsonObject(const std::string& name,
                                                                 const ValueDetails& details,
                                                                 mono_float current_value) {
  nlohmann::json j;

  try {
    // Validate string lengths before using them (prevent corrupted strings)
    if (name.length() > 10000 || details.display_name.length() > 10000 ||
        details.display_units.length() > 10000) {
      DBG("Warning: Parameter has suspiciously long string, skipping");
      j = {{"name", "corrupted"}, {"error", "string data corrupted"}};
      return j;
    }

    j = {
      {"name", name},
      {"display_name", details.display_name},
      {"value", static_cast<double>(current_value)},
      {"min", static_cast<double>(details.min)},
      {"max", static_cast<double>(details.max)},
      {"default", static_cast<double>(details.default_value)},
      {"is_automation_parameter", true}
    };

    // Add optional fields
    if (!details.display_units.empty()) {
      j["units"] = details.display_units;
    }
  } catch (const std::exception& e) {
    DBG("Error creating JSON for parameter " << name.c_str() << ": " << e.what());
    // Return minimal valid JSON
    j = {{"name", "error"}, {"error", "failed to serialize"}};
    return j;
  }

  if (details.string_lookup != nullptr) {
    nlohmann::json string_lookup_array = nlohmann::json::array();
    try {
      int count = 0;
      // Safety limit: max 1000 entries to prevent reading garbage memory
      while (count < 1000 && !details.string_lookup[count].empty()) {
        string_lookup_array.push_back(details.string_lookup[count]);
        count++;
      }
      j["string_lookup"] = string_lookup_array;
    } catch (const std::exception& e) {
      DBG("Warning: Error reading string_lookup for parameter " << name.c_str() << ": " << e.what());
      // Skip string_lookup if there's an error
    }
  }

  if (details.version_added > 0) {
    j["version_added"] = details.version_added;
  }

  return j;
}

} // namespace vital
