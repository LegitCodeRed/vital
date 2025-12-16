/**
 * Type definitions for Vital MCP Server
 */

export interface VitalParameter {
  name: string;
  value: number;
  min: number;
  max: number;
  display_name?: string;
  display_value?: string;
  category?: string;
}

export interface McpMessage {
  jsonrpc: string;
  method?: string;
  params?: any;
  id?: string | number;
  result?: any;
  error?: any;
  is_response?: boolean;
}

export interface SetParameterRequest {
  name: string;
  value: number;
}

export interface BatchSetParametersRequest {
  parameters: SetParameterRequest[];
}

export interface ParameterChangedNotification {
  name: string;
  value: number;
  timestamp?: number;
}
