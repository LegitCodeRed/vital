/**
 * Vital MCP Server - Main server implementation
 * Handles bidirectional communication with Vital C++ application via stdio
 */

import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import {
  CallToolRequestSchema,
  ListToolsRequestSchema,
  ListResourcesRequestSchema,
  ReadResourceRequestSchema
} from '@modelcontextprotocol/sdk/types.js';
import readline from 'readline';
import logger from './logger.js';
import { tools, handleListParameters, handleGetParameter, handleSetParameter, handleBatchSetParameters } from './tools.js';
import { resources, handleResourceRead } from './resources.js';
import { McpMessage } from './types.js';

export class VitalMcpServer {
  private server: Server;
  private transport: StdioServerTransport | null = null;

  constructor() {
    this.server = new Server(
      {
        name: 'vital-mcp-server',
        version: '1.0.0'
      },
      {
        capabilities: {
          tools: {},
          resources: {}
        }
      }
    );

    this.setupHandlers();
    logger.info('VitalMcpServer initialized');
  }

  private setupHandlers(): void {
    // List available tools
    this.server.setRequestHandler(ListToolsRequestSchema, async () => {
      logger.debug('Listing tools');
      return { tools };
    });

    // Handle tool calls
    this.server.setRequestHandler(CallToolRequestSchema, async (request) => {
      logger.info(`Tool called: ${request.params.name}`);
      
      try {
        switch (request.params.name) {
          case 'list_parameters':
            return {
              content: [{
                type: 'text',
                text: JSON.stringify(await handleListParameters())
              }]
            };
            
          case 'get_parameter':
            return {
              content: [{
                type: 'text',
                text: JSON.stringify(await handleGetParameter(request.params.arguments))
              }]
            };
            
          case 'set_parameter':
            return {
              content: [{
                type: 'text',
                text: JSON.stringify(await handleSetParameter(request.params.arguments))
              }]
            };
            
          case 'batch_set_parameters':
            return {
              content: [{
                type: 'text',
                text: JSON.stringify(await handleBatchSetParameters(request.params.arguments))
              }]
            };
            
          default:
            throw new Error(`Unknown tool: ${request.params.name}`);
        }
      } catch (error: any) {
        logger.error('Tool execution error:', error);
        return {
          content: [{
            type: 'text',
            text: JSON.stringify({
              success: false,
              error: error.message
            })
          }],
          isError: true
        };
      }
    });

    // List available resources
    this.server.setRequestHandler(ListResourcesRequestSchema, async () => {
      logger.debug('Listing resources');
      return { resources };
    });

    // Read resource contents
    this.server.setRequestHandler(ReadResourceRequestSchema, async (request) => {
      logger.info(`Reading resource: ${request.params.uri}`);
      return handleResourceRead(request.params.uri);
    });
  }

  /**
   * Start the server with stdio transport
   */
  async start(): Promise<void> {
    this.transport = new StdioServerTransport();
    await this.server.connect(this.transport);
    
    // Send server_ready notification to C++
    this.sendToCpp({
      jsonrpc: '2.0',
      method: 'server_ready',
      params: { port: 3000 }
    });
    
    logger.info('MCP Server started with stdio transport');
  }

  /**
   * Stop the server
   */
  async stop(): Promise<void> {
    if (this.transport) {
      await this.server.close();
      this.transport = null;
    }
    logger.info('MCP Server stopped');
  }

  /**
   * Send a message to C++ via stdout
   * Messages are newline-delimited JSON
   */
  private sendToCpp(message: McpMessage): void {
    const json = JSON.stringify(message);
    process.stdout.write(json + '\n');
    logger.debug('Sent to C++:', json);
  }

  /**
   * Listen for messages from C++ via stdin
   * This allows C++ to send parameter_changed notifications
   */
  listenForCppMessages(): void {
    const rl = readline.createInterface({
      input: process.stdin,
      terminal: false
    });

    rl.on('line', (line: string) => {
      try {
        const message: McpMessage = JSON.parse(line);
        logger.debug('Received from C++:', message);
        
        // Handle different message types from C++
        if (message.method === 'parameter_changed') {
          // Forward parameter change notifications to MCP clients
          this.handleParameterChanged(message.params);
        } else if (message.method === 'ping') {
          // Respond to heartbeat
          this.sendToCpp({
            jsonrpc: '2.0',
            method: 'pong',
            params: { id: message.params?.id }
          });
        }
      } catch (error: any) {
        logger.error('Failed to parse message from C++:', error);
      }
    });

    rl.on('close', () => {
      logger.info('stdin closed, shutting down server');
      this.stop();
      process.exit(0);
    });
  }

  /**
   * Handle parameter change notifications from C++
   * Forward these as notifications to MCP clients
   */
  private handleParameterChanged(params: any): void {
    logger.info(`Parameter changed: ${params.name} = ${params.value}`);
    // Note: Notifications to MCP clients would go through the SDK's notification system
    // This is a placeholder for the full bidirectional notification implementation
  }
}
