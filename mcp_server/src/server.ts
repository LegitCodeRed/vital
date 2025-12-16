/**
 * Vital MCP Server Implementation
 *
 * This class implements the MCP server for Vital synthesizer.
 * It handles tool calls from MCP clients and communicates with
 * the C++ application via stdin/stdout.
 */

import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
  ListResourcesRequestSchema,
  ReadResourceRequestSchema,
} from '@modelcontextprotocol/sdk/types.js';
import * as readline from 'readline';
import {
  ServerConfig,
  ParameterMetadata,
  JsonRpcMessage,
  ErrorCode,
  ToolResult,
} from './types.js';

export class VitalMcpServer {
  private server: Server;
  private config: ServerConfig;
  private parameters: Map<string, ParameterMetadata> = new Map();
  private pendingRequests: Map<string, (result: any) => void> = new Map();
  private requestCounter = 0;
  private heartbeatTimer?: NodeJS.Timeout;
  private readlineInterface?: readline.Interface;

  constructor(server: Server, config: ServerConfig) {
    this.server = server;
    this.config = config;

    this.setupHandlers();
  }

  /**
   * Initialize the server and set up stdio communication
   */
  async initialize(): Promise<void> {
    // Set up readline for stdin messages from C++
    this.readlineInterface = readline.createInterface({
      input: process.stdin,
      output: process.stdout,
      terminal: false,
    });

    this.readlineInterface.on('line', (line: string) => {
      try {
        const message: JsonRpcMessage = JSON.parse(line);
        this.handleCppMessage(message);
      } catch (error) {
        console.error('[VitalMcpServer] Failed to parse message from C++:', error);
      }
    });

    // Start heartbeat
    this.startHeartbeat();

    // Request initial parameter list
    await this.requestParameterList();
  }

  /**
   * Shutdown the server gracefully
   */
  async shutdown(): Promise<void> {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
    }

    if (this.readlineInterface) {
      this.readlineInterface.close();
    }
  }

  /**
   * Set up MCP request handlers
   */
  private setupHandlers(): void {
    // List available tools
    this.server.setRequestHandler(ListToolsRequestSchema, async () => ({
      tools: [
        {
          name: 'list_parameters',
          description: 'List all available Vital synthesizer parameters with metadata',
          inputSchema: {
            type: 'object',
            properties: {},
          },
        },
        {
          name: 'get_parameter',
          description: 'Get the current value of a specific parameter',
          inputSchema: {
            type: 'object',
            properties: {
              name: {
                type: 'string',
                description: 'The parameter name (e.g., "osc_1_level")',
              },
            },
            required: ['name'],
          },
        },
        {
          name: 'set_parameter',
          description: 'Set the value of a specific parameter',
          inputSchema: {
            type: 'object',
            properties: {
              name: {
                type: 'string',
                description: 'The parameter name',
              },
              value: {
                type: 'number',
                description: 'The normalized value (0.0 to 1.0)',
              },
            },
            required: ['name', 'value'],
          },
        },
        {
          name: 'batch_set_parameters',
          description: 'Set multiple parameters atomically',
          inputSchema: {
            type: 'object',
            properties: {
              parameters: {
                type: 'array',
                items: {
                  type: 'object',
                  properties: {
                    name: { type: 'string' },
                    value: { type: 'number' },
                  },
                  required: ['name', 'value'],
                },
              },
            },
            required: ['parameters'],
          },
        },
      ],
    }));

    // Handle tool calls
    this.server.setRequestHandler(CallToolRequestSchema, async (request) => {
      const { name, arguments: args } = request.params;

      try {
        switch (name) {
          case 'list_parameters':
            return await this.handleListParameters();
          case 'get_parameter':
            return await this.handleGetParameter(args);
          case 'set_parameter':
            return await this.handleSetParameter(args);
          case 'batch_set_parameters':
            return await this.handleBatchSetParameters(args);
          default:
            throw new Error(`Unknown tool: ${name}`);
        }
      } catch (error) {
        return {
          content: [
            {
              type: 'text',
              text: `Error: ${error instanceof Error ? error.message : String(error)}`,
            },
          ],
          isError: true,
        };
      }
    });

    // List available resources
    this.server.setRequestHandler(ListResourcesRequestSchema, async () => ({
      resources: [
        {
          uri: 'vital://parameter_schema',
          name: 'Parameter Schema',
          description: 'Complete schema of all Vital parameters with metadata',
          mimeType: 'application/json',
        },
        {
          uri: 'vital://current_state',
          name: 'Current State',
          description: 'Current values of all parameters',
          mimeType: 'application/json',
        },
      ],
    }));

    // Handle resource reads
    this.server.setRequestHandler(ReadResourceRequestSchema, async (request) => {
      const { uri } = request.params;

      switch (uri) {
        case 'vital://parameter_schema':
          return {
            contents: [
              {
                uri,
                mimeType: 'application/json',
                text: JSON.stringify(Array.from(this.parameters.values()), null, 2),
              },
            ],
          };
        case 'vital://current_state':
          const currentState = Array.from(this.parameters.entries()).map(([name, meta]) => ({
            name,
            value: meta.value,
          }));
          return {
            contents: [
              {
                uri,
                mimeType: 'application/json',
                text: JSON.stringify(currentState, null, 2),
              },
            ],
          };
        default:
          throw new Error(`Unknown resource: ${uri}`);
      }
    });
  }

  /**
   * Tool Handlers
   */

  private async handleListParameters(): Promise<any> {
    const params = Array.from(this.parameters.values());
    return {
      content: [
        {
          type: 'text',
          text: JSON.stringify(params, null, 2),
        },
      ],
    };
  }

  private async handleGetParameter(args: any): Promise<any> {
    const { name } = args;
    const param = this.parameters.get(name);

    if (!param) {
      throw new Error(`Parameter not found: ${name}`);
    }

    return {
      content: [
        {
          type: 'text',
          text: JSON.stringify(param, null, 2),
        },
      ],
    };
  }

  private async handleSetParameter(args: any): Promise<any> {
    const { name, value } = args;

    // Validate parameter exists
    if (!this.parameters.has(name)) {
      throw new Error(`Parameter not found: ${name}`);
    }

    // Validate value range
    if (typeof value !== 'number' || value < 0 || value > 1) {
      throw new Error(`Invalid value: ${value}. Must be between 0.0 and 1.0`);
    }

    // Send set_parameter request to C++
    const result = await this.sendToCpp('set_parameter', { name, value });

    return {
      content: [
        {
          type: 'text',
          text: JSON.stringify(result),
        },
      ],
    };
  }

  private async handleBatchSetParameters(args: any): Promise<any> {
    const { parameters } = args;

    // Validate all parameters
    for (const param of parameters) {
      if (!this.parameters.has(param.name)) {
        throw new Error(`Parameter not found: ${param.name}`);
      }
      if (typeof param.value !== 'number' || param.value < 0 || param.value > 1) {
        throw new Error(`Invalid value for ${param.name}: ${param.value}`);
      }
    }

    // Send batch request to C++
    const result = await this.sendToCpp('batch_set_parameters', { parameters });

    return {
      content: [
        {
          type: 'text',
          text: JSON.stringify(result),
        },
      ],
    };
  }

  /**
   * Communication with C++ Application
   */

  private async sendToCpp(method: string, params: any): Promise<any> {
    return new Promise((resolve, reject) => {
      const id = `req-${++this.requestCounter}`;

      const message: JsonRpcMessage = {
        jsonrpc: '2.0',
        method,
        params,
        id,
      };

      // Store pending request
      this.pendingRequests.set(id, resolve);

      // Send to C++ via stdout
      console.log(JSON.stringify(message));

      // Set timeout
      setTimeout(() => {
        if (this.pendingRequests.has(id)) {
          this.pendingRequests.delete(id);
          reject(new Error(`Request timeout: ${method}`));
        }
      }, 5000);
    });
  }

  private handleCppMessage(message: JsonRpcMessage): void {
    // Handle response to our request
    if (message.id && this.pendingRequests.has(message.id as string)) {
      const resolve = this.pendingRequests.get(message.id as string);
      this.pendingRequests.delete(message.id as string);

      if (message.error) {
        resolve!({ success: false, error: message.error });
      } else {
        resolve!(message.result);
      }
      return;
    }

    // Handle notifications from C++
    if (message.method) {
      switch (message.method) {
        case 'parameter_changed':
          this.handleParameterChanged(message.params);
          break;
        case 'parameter_list':
          this.handleParameterList(message.params);
          break;
        case 'ping':
          this.handlePing(message.id as string);
          break;
        default:
          console.error('[VitalMcpServer] Unknown method from C++:', message.method);
      }
    }
  }

  private handleParameterChanged(params: any): void {
    const { name, value } = params;

    // Update local cache
    const param = this.parameters.get(name);
    if (param) {
      param.value = value;
    }

    // TODO: Send notification to MCP clients if needed
    console.error(`[VitalMcpServer] Parameter changed: ${name} = ${value}`);
  }

  private handleParameterList(params: any): void {
    const { parameters } = params;

    // Update parameter cache
    this.parameters.clear();
    for (const param of parameters) {
      this.parameters.set(param.name, param);
    }

    console.error(`[VitalMcpServer] Loaded ${this.parameters.size} parameters`);
  }

  private handlePing(id: string): void {
    // Respond to heartbeat ping
    const pong: JsonRpcMessage = {
      jsonrpc: '2.0',
      result: { pong: true },
      id,
    };
    console.log(JSON.stringify(pong));
  }

  /**
   * Heartbeat
   */

  private startHeartbeat(): void {
    this.heartbeatTimer = setInterval(() => {
      // Send heartbeat notification
      const heartbeat: JsonRpcMessage = {
        jsonrpc: '2.0',
        method: 'heartbeat',
        params: { timestamp: Date.now() },
      };
      console.log(JSON.stringify(heartbeat));
    }, this.config.heartbeat_interval_ms);
  }

  /**
   * Request parameter list from C++
   */
  private async requestParameterList(): Promise<void> {
    try {
      await this.sendToCpp('get_all_parameters', {});
    } catch (error) {
      console.error('[VitalMcpServer] Failed to request parameter list:', error);
    }
  }
}
