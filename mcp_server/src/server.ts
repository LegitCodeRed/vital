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
import express from 'express';
import http from 'http';
import logger from './logger.js';
import { tools, handleListParameters, handleGetParameter, handleSetParameter, handleBatchSetParameters } from './tools.js';
import { resources, handleResourceRead } from './resources.js';
import { McpMessage } from './types.js';

export class VitalMcpServer {
  private server: Server;
  private transport: StdioServerTransport | null = null;
  private httpServer: http.Server | null = null;
  private app: express.Application;
  private port: number = 3000;

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

    // Set up Express app for HTTP communication with C++
    this.app = express();
    this.app.use(express.json());
    this.setupHttpRoutes();

    this.setupHandlers();
    logger.info('VitalMcpServer initialized');
  }

  /**
   * Set up HTTP routes for C++ communication
   */
  private setupHttpRoutes(): void {
    // Heartbeat endpoint
    this.app.post('/api/heartbeat', (req, res) => {
      res.json({ status: 'ok', timestamp: Date.now() });
    });

    // RPC endpoint for JSON-RPC messages from C++
    this.app.post('/api/rpc', async (req, res) => {
      try {
        const message: McpMessage = req.body;
        logger.debug('Received RPC from C++:', message);

        // Handle different methods
        if (message.method === 'parameter_changed') {
          this.handleParameterChanged(message.params);
          res.json({ success: true });
        } else if (message.method === 'metadata_parameters') {
          // Store parameter metadata
          logger.info(`Received metadata for ${message.params.parameters?.length || 0} parameters`);
          res.json({ success: true });
        } else {
          res.status(400).json({ error: 'Unknown method' });
        }
      } catch (error: any) {
        logger.error('Error handling RPC:', error);
        res.status(500).json({ error: error.message });
      }
    });

    // Endpoint for C++ to get pending parameter changes (polling)
    this.app.get('/api/pending_changes', (req, res) => {
      // For now, return empty array - full implementation would track changes
      res.json({ parameters: [] });
    });

    // Metadata endpoint
    this.app.post('/api/metadata/parameters', (req, res) => {
      logger.info(`Received metadata for ${req.body.parameters?.length || 0} parameters`);
      res.json({ success: true });
    });

    // Parameter change notification endpoint
    this.app.post('/api/notify/parameter_changed', (req, res) => {
      const { name, value } = req.body;
      logger.info(`Parameter changed: ${name} = ${value}`);
      this.handleParameterChanged({ name, value });
      res.json({ success: true });
    });
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
   * Start the server with HTTP server
   */
  async start(): Promise<void> {
    // Parse port from command line args
    const portArg = process.argv.find(arg => arg.startsWith('--port='));
    if (portArg) {
      this.port = parseInt(portArg.split('=')[1], 10);
    }

    // Start HTTP server for C++ communication
    await new Promise<void>((resolve, reject) => {
      this.httpServer = this.app.listen(this.port, () => {
        logger.info(`HTTP server listening on port ${this.port} for C++ communication`);
        resolve();
      }).on('error', reject);
    });

    // TODO: Add MCP SDK transport later (SSE or WebSocket instead of stdio)
    // For now, only HTTP server is running for C++ communication
    // this.transport = new StdioServerTransport();
    // await this.server.connect(this.transport);

    logger.info('MCP Server started with HTTP server');
  }

  /**
   * Stop the server
   */
  async stop(): Promise<void> {
    if (this.httpServer) {
      await new Promise<void>((resolve) => {
        this.httpServer!.close(() => resolve());
      });
      this.httpServer = null;
    }

    if (this.transport) {
      await this.server.close();
      this.transport = null;
    }

    logger.info('MCP Server stopped');
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
