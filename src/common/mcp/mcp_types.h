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
#include "json/json.h"

namespace vital {

/**
 * Status of the MCP server process
 */
enum class McpServerStatus {
  Stopped,    // Server is not running
  Starting,   // Server is being started
  Running,    // Server is running and healthy
  Error       // Server encountered an error
};

/**
 * Message structure for JSON-RPC 2.0 communication with MCP server
 * Messages are newline-delimited JSON objects sent over stdio
 */
struct McpMessage {
  std::string jsonrpc;    // Always "2.0"
  std::string method;     // Method name (e.g., "set_parameter", "parameter_changed")
  nlohmann::json params;  // Parameters object
  std::string id;         // Request ID (empty for notifications)
  bool is_response;       // True if this is a response to our request
  nlohmann::json result;  // Result object (for responses)
  nlohmann::json error;   // Error object (for error responses)

  McpMessage() : jsonrpc("2.0"), is_response(false) {}

  /**
   * Serialize message to JSON string
   */
  std::string toJson() const {
    nlohmann::json j;
    j["jsonrpc"] = jsonrpc;

    if (is_response) {
      // Response format
      if (!error.is_null()) {
        j["error"] = error;
      } else {
        j["result"] = result;
      }
      if (!id.empty()) {
        j["id"] = id;
      }
    } else {
      // Request or notification format
      j["method"] = method;
      if (!params.is_null()) {
        j["params"] = params;
      }
      if (!id.empty()) {
        j["id"] = id;
      }
    }

    return j.dump();
  }

  /**
   * Deserialize message from JSON string
   */
  static McpMessage fromJson(const std::string& json_str) {
    McpMessage msg;
    try {
      auto j = nlohmann::json::parse(json_str);

      msg.jsonrpc = j.value("jsonrpc", "2.0");
      msg.id = j.value("id", "");

      // Check if this is a response (has result or error)
      if (j.contains("result") || j.contains("error")) {
        msg.is_response = true;
        if (j.contains("result")) {
          msg.result = j["result"];
        }
        if (j.contains("error")) {
          msg.error = j["error"];
        }
      } else {
        // Request or notification
        msg.is_response = false;
        msg.method = j.value("method", "");
        if (j.contains("params")) {
          msg.params = j["params"];
        }
      }
    } catch (const nlohmann::json::exception& e) {
      // Return invalid message
      msg.method = "parse_error";
      msg.error = {{"code", -32700}, {"message", e.what()}};
    }

    return msg;
  }

  /**
   * Create a request message
   */
  static McpMessage createRequest(const std::string& method,
                                   const nlohmann::json& params = nlohmann::json(),
                                   const std::string& id = "") {
    McpMessage msg;
    msg.method = method;
    msg.params = params;
    msg.id = id.empty() ? generateId() : id;
    msg.is_response = false;
    return msg;
  }

  /**
   * Create a notification message (no response expected)
   */
  static McpMessage createNotification(const std::string& method,
                                        const nlohmann::json& params = nlohmann::json()) {
    McpMessage msg;
    msg.method = method;
    msg.params = params;
    msg.id = "";  // Notifications have no ID
    msg.is_response = false;
    return msg;
  }

  /**
   * Create a success response message
   */
  static McpMessage createResponse(const std::string& id,
                                    const nlohmann::json& result) {
    McpMessage msg;
    msg.id = id;
    msg.result = result;
    msg.is_response = true;
    return msg;
  }

  /**
   * Create an error response message
   */
  static McpMessage createError(const std::string& id,
                                 int code,
                                 const std::string& message) {
    McpMessage msg;
    msg.id = id;
    msg.error = {{"code", code}, {"message", message}};
    msg.is_response = true;
    return msg;
  }

private:
  /**
   * Generate a unique request ID
   */
  static std::string generateId() {
    static int counter = 0;
    return "req-" + std::to_string(++counter);
  }
};

/**
 * JSON-RPC 2.0 error codes
 */
namespace McpErrorCode {
  constexpr int ParseError = -32700;      // Invalid JSON
  constexpr int InvalidRequest = -32600;  // Invalid Request object
  constexpr int MethodNotFound = -32601;  // Method not found
  constexpr int InvalidParams = -32602;   // Invalid method parameters
  constexpr int InternalError = -32603;   // Internal JSON-RPC error

  // Custom application errors
  constexpr int ParameterNotFound = -32000;
  constexpr int ParameterReadOnly = -32001;
  constexpr int InvalidValue = -32002;
}

} // namespace vital
