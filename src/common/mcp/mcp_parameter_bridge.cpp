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

  // Start polling timer on message thread
  MessageManager::callAsync([this]() {
    startTimer(kPollIntervalMs);
  });

  // Send parameter metadata to MCP server
  sendAllParameterMetadata();
}

void McpParameterBridge::stop() {
  if (!is_running_) {
    return;
  }

  is_running_ = false;

  // Stop timer on message thread
  MessageManager::callAsync([this]() {
    stopTimer();
  });

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
    DBG("Cannot send metadata: bridge not running or MCP server not available");
    return;
  }

  DBG("Starting to send parameter metadata...");

  try {
    // Build array of all parameter metadata
    nlohmann::json parameters_array = nlohmann::json::array();

    int num_params = parameter_lookup_.getNumParameters();
    DBG("Total parameters to send: " << num_params);

    for (int i = 0; i < num_params; ++i) {
      const ValueDetails* details = parameter_lookup_.getDetails(i);
      if (details) {
        try {
          // Get current value from synth
          mono_float current_value = 0.0f;
          auto& controls = synth_base_->getControls();
          auto control_it = controls.find(details->name);
          if (control_it != controls.end()) {
            current_value = control_it->second->value();
          }

          nlohmann::json param_json = parameterDetailsToJsonObject(details->name, *details, current_value);

          // Skip error entries
          if (param_json.count("error") == 0) {
            parameters_array.push_back(param_json);
          }
        } catch (const std::exception& e) {
          DBG("Error serializing parameter " << i << ": " << e.what());
          continue;
        }
      }
    }

    DBG("Successfully serialized " << parameters_array.size() << " parameters");

    // Send to MCP server via HTTP
    nlohmann::json body = {
      {"parameters", parameters_array}
    };

    std::string json_str = body.dump();
    String json_body(json_str.c_str());

    DBG("Sending metadata to MCP server... (payload size: " << json_str.length() << " bytes)");
    String response = mcp_manager_->httpPost("/api/metadata/parameters", json_body);

    if (response.isNotEmpty()) {
      DBG("Metadata sent successfully. Response: " << response);
      metadata_sent_ = true;
    } else {
      DBG("Failed to send metadata - empty response");
    }
  } catch (const std::exception& e) {
    DBG("FATAL: Error in sendAllParameterMetadata: " << e.what());
  }
}

void McpParameterBridge::timerCallback() {
  if (!is_running_ || !mcp_manager_ || !mcp_manager_->isServerRunning()) {
    return;
  }

  // Poll for parameter change requests from MCP server (async)
  pollParameterChanges();
  pollWavetableImports();
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
        auto params = json_response["parameters"];
        if (params.size() > 0) {
          DBG("Received " << params.size() << " parameter changes from MCP");
        }

        for (const auto& param : params) {
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

void McpParameterBridge::pollWavetableImports() {
  auto manager = mcp_manager_;
  auto self = this;

  Thread::launch([manager, self]() {
    String response = manager->httpGet("/api/pending_wavetables");
    if (response.isEmpty())
      return;

    try {
      auto json_response = nlohmann::json::parse(response.toStdString());
      auto it = json_response.find("wavetables");
      if (it == json_response.end() || !it->is_array())
        return;

      for (const auto& wavetable : *it) {
        std::string path = wavetable.value("path", std::string());
        int oscillator = wavetable.value("oscillator", 1);
        self->applyWavetableImport(path, oscillator);
      }
    }
    catch (const nlohmann::json::exception& e) {
      DBG("Failed to parse wavetable import payload: " << e.what());
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

  DBG("Applying MCP parameter change: " << name.c_str() << " = " << clamped_value);

  // Mark as pending to prevent echo
  {
    const ScopedLock lock(pending_changes_lock_);
    pending_changes_.insert(name);
  }

  // Apply change to Vital via SynthBase on message thread to avoid threading issues
  MessageManager::callAsync([this, name, clamped_value]() {
    DBG("Executing parameter change on message thread: " << name.c_str());
    synth_base_->valueChangedExternal(name, clamped_value);
  });
}

void McpParameterBridge::applyWavetableImport(const std::string& path, int oscillator) {
  if (!is_running_ || !mcp_manager_ || !mcp_manager_->isServerRunning())
    return;

  if (oscillator < 1 || oscillator > vital::kNumOscillators) {
    DBG("Invalid oscillator index for wavetable import: " << oscillator);
    return;
  }

  File wavetable_file(path);
  if (!wavetable_file.existsAsFile()) {
    DBG("Wavetable file missing: " << path.c_str());
    return;
  }

  int64 file_size = wavetable_file.getSize();
  if (file_size <= 0 || file_size > kMaxWavetableBytes) {
    DBG("Wavetable file has invalid size: " << file_size);
    return;
  }

  auto self = this;
  MessageManager::callAsync([self, path, oscillator]() {
    File file(path);
    FileInputStream stream(file);
    if (!stream.openedOk()) {
      DBG("Failed to open wavetable file for import: " << path.c_str());
      return;
    }

    MemoryBlock block;
    if (!stream.readIntoMemoryBlock(block)) {
      DBG("Failed to read wavetable file: " << path.c_str());
      return;
    }

    std::string json_text(static_cast<const char*>(block.getData()), block.getSize());
    nlohmann::json json_data;
    try {
      json_data = nlohmann::json::parse(json_text);
    }
    catch (const std::exception& e) {
      DBG("Failed to parse wavetable JSON: " << e.what());
      return;
    }

    WavetableCreator* creator = self->synth_base_->getWavetableCreator(oscillator - 1);
    if (!creator) {
      DBG("No wavetable creator available for oscillator: " << oscillator);
      return;
    }

    try {
      creator->clear();
      creator->jsonToState(json_data);
      creator->render();
      DBG("Imported wavetable into oscillator " << oscillator << " from " << path.c_str());
    }
    catch (const std::exception& e) {
      DBG("Error applying wavetable import: " << e.what());
    }
  });
}

// Helper to sanitize strings - replace any null bytes and non-printable chars
static std::string sanitizeString(const std::string& input) {
  std::string result;
  result.reserve(input.length());

  for (char c : input) {
    // Only keep printable ASCII and common whitespace
    if ((c >= 32 && c <= 126) || c == '\t' || c == '\n' || c == '\r') {
      result += c;
    } else {
      result += '?'; // Replace invalid chars
    }
  }

  // Limit length
  if (result.length() > 256) {
    result = result.substr(0, 256);
  }

  return result;
}

nlohmann::json McpParameterBridge::parameterDetailsToJsonObject(const std::string& name,
                                                                 const ValueDetails& details,
                                                                 mono_float current_value) {
  nlohmann::json j;

  try {
    // Sanitize all strings before use
    std::string safe_name = sanitizeString(name);
    std::string safe_display_name = sanitizeString(details.display_name);
    std::string safe_units = sanitizeString(details.display_units);

    j = {
      {"name", safe_name},
      {"display_name", safe_display_name},
      {"value", static_cast<double>(current_value)},
      {"min", static_cast<double>(details.min)},
      {"max", static_cast<double>(details.max)},
      {"default", static_cast<double>(details.default_value)},
      {"is_automation_parameter", true}
    };

    // Add optional fields
    if (!safe_units.empty()) {
      j["units"] = safe_units;
    }
  } catch (const std::exception& e) {
    DBG("Error creating JSON for parameter: " << e.what());
    // Return minimal valid JSON
    j = {{"name", "error"}, {"error", "failed to serialize"}};
    return j;
  }

  if (details.string_lookup != nullptr) {
    nlohmann::json string_lookup_array = nlohmann::json::array();
    try {
      int count = 0;
      // Safety limit: max 100 entries to prevent reading garbage memory
      while (count < 100 && !details.string_lookup[count].empty()) {
        std::string sanitized = sanitizeString(details.string_lookup[count]);
        if (!sanitized.empty()) {
          string_lookup_array.push_back(sanitized);
        }
        count++;
      }
      if (string_lookup_array.size() > 0) {
        j["string_lookup"] = string_lookup_array;
      }
    } catch (const std::exception& e) {
      DBG("Warning: Error reading string_lookup: " << e.what());
      // Skip string_lookup if there's an error
    }
  }

  if (details.version_added > 0) {
    j["version_added"] = details.version_added;
  }

  return j;
}

} // namespace vital
