/**
 * Vital MCP Server Implementation (HTTP/REST version)
 *
 * This class manages parameter state and handles requests from both
 * the C++ application and MCP clients.
 */

import {
  ServerConfig,
  ParameterMetadata,
} from './types.js';

export class VitalMcpServer {
  private config: ServerConfig;
  private parameters: Map<string, ParameterMetadata> = new Map();
  private pendingChanges: Array<{ name: string; value: number }> = [];

  constructor(config: ServerConfig) {
    this.config = config;
    console.log('[VitalMcpServer] Initialized with config:', config);
  }

  /**
   * List all parameters
   */
  listParameters(): ParameterMetadata[] {
    return Array.from(this.parameters.values());
  }

  /**
   * Get a specific parameter
   */
  getParameter(name: string): ParameterMetadata {
    const param = this.parameters.get(name);
    if (!param) {
      throw new Error(`Parameter not found: ${name}`);
    }
    return param;
  }

  /**
   * Set a parameter value (called from HTTP API)
   * This will be forwarded to C++ via polling
   */
  setParameter(name: string, value: number): void {
    if (!this.parameters.has(name)) {
      // If parameter doesn't exist yet, create a placeholder
      this.parameters.set(name, {
        name,
        display_name: name,
        value,
        min: 0,
        max: 1,
        default: 0.5,
        is_automation_parameter: true,
      });
    } else {
      // Update existing parameter
      const param = this.parameters.get(name)!;
      param.value = value;
    }

    // Queue this change for C++ to pick up
    this.pendingChanges.push({ name, value });

    console.log(`[VitalMcpServer] Parameter set: ${name} = ${value}`);
  }

  /**
   * Batch set multiple parameters
   */
  batchSetParameters(parameters: Array<{ name: string; value: number }>): void {
    for (const param of parameters) {
      this.setParameter(param.name, param.value);
    }
  }

  /**
   * Called when C++ notifies us of a parameter change
   */
  onParameterChanged(name: string, value: number): void {
    if (!this.parameters.has(name)) {
      // First time seeing this parameter, create it
      this.parameters.set(name, {
        name,
        display_name: name,
        value,
        min: 0,
        max: 1,
        default: 0.5,
        is_automation_parameter: true,
      });
    } else {
      // Update existing parameter
      const param = this.parameters.get(name)!;
      param.value = value;
    }

    console.log(`[VitalMcpServer] Parameter changed notification: ${name} = ${value}`);
  }

  /**
   * Update parameter metadata from C++
   */
  updateParameterMetadata(params: ParameterMetadata[]): void {
    for (const param of params) {
      this.parameters.set(param.name, param);
    }
    console.log(`[VitalMcpServer] Updated metadata for ${params.length} parameters`);
  }

  /**
   * Get pending parameter changes and clear the queue
   * Called by C++ via polling
   */
  getPendingChanges(): Array<{ name: string; value: number }> {
    const changes = [...this.pendingChanges];
    this.pendingChanges = [];
    return changes;
  }
}
