/**
 * TypeScript type definitions for Vital MCP Server
 */

/**
 * JSON-RPC 2.0 message structure
 */
export interface JsonRpcMessage {
  jsonrpc: '2.0';
  method?: string;
  params?: any;
  id?: string | number;
  result?: any;
  error?: JsonRpcError;
}

/**
 * JSON-RPC 2.0 error structure
 */
export interface JsonRpcError {
  code: number;
  message: string;
  data?: any;
}

/**
 * JSON-RPC 2.0 error codes
 */
export enum ErrorCode {
  ParseError = -32700,
  InvalidRequest = -32600,
  MethodNotFound = -32601,
  InvalidParams = -32602,
  InternalError = -32603,
  // Custom application errors
  ParameterNotFound = -32000,
  ParameterReadOnly = -32001,
  InvalidValue = -32002,
}

/**
 * Vital parameter metadata
 */
export interface ParameterMetadata {
  name: string;
  display_name: string;
  value: number;
  min: number;
  max: number;
  default: number;
  is_automation_parameter: boolean;
  string_lookup?: string[];
  decimal_places?: number;
  units?: string;
  version_added?: number;
  hidden?: boolean;
}

/**
 * Parameter change notification
 */
export interface ParameterChangeNotification {
  name: string;
  value: number;
  normalized_value: number;
}

/**
 * Batch parameter set request
 */
export interface BatchSetRequest {
  parameters: Array<{
    name: string;
    value: number;
  }>;
}

/**
 * MCP tool call result
 */
export interface ToolResult {
  success: boolean;
  message?: string;
  data?: any;
}

/**
 * Server configuration
 */
export interface ServerConfig {
  port: number;
  log_level: 'debug' | 'info' | 'warn' | 'error';
  heartbeat_interval_ms: number;
}
