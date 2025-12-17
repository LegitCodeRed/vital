/**
 * Vital MCP Server - Main server implementation
 * Handles bidirectional communication with Vital C++ application via stdio
 */

import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { SSEServerTransport } from '@modelcontextprotocol/sdk/server/sse.js';
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
import { parameterStore } from './parameter-store.js';

export class VitalMcpServer {
  private server: Server;
  private transport: StdioServerTransport | SSEServerTransport | null = null;
  private httpServer: http.Server | null = null;
  private app: express.Application;
  private port: number = 3000;
  private useStdio: boolean = false; // Use stdio if no port specified or if stdin is a TTY
  private pendingParameterChanges: Array<{ name: string; value: number }> = [];

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
    this.app.use(express.json({ limit: '50mb' })); // Increase limit for parameter metadata
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
      // Return and clear pending changes
      const changes = [...this.pendingParameterChanges];
      this.pendingParameterChanges = [];
      res.json({ parameters: changes });
    });

    // Metadata endpoint - store parameter metadata from C++
    this.app.post('/api/metadata/parameters', (req, res) => {
      const params = req.body.parameters || [];
      logger.info(`Received metadata for ${params.length} parameters`);
      parameterStore.setParameters(params);
      logger.info(`Parameter store now contains ${parameterStore.getParameterCount()} parameters`);
      res.json({ success: true });
    });

    // Parameter change notification endpoint
    this.app.post('/api/notify/parameter_changed', (req, res) => {
      const { name, value } = req.body;
      logger.info(`Parameter changed: ${name} = ${value}`);
      this.handleParameterChanged({ name, value });
      res.json({ success: true });
    });

    // MCP HTTP endpoints for bridge - directly expose MCP functionality
    this.app.post('/mcp/tools/list', async (req, res) => {
      try {
        res.json({ tools });
      } catch (error: any) {
        res.status(500).json({ error: error.message });
      }
    });

    this.app.post('/mcp/tools/call', async (req, res) => {
      try {
        const { name, arguments: args } = req.body;

        switch (name) {
          case 'list_parameters':
            res.json({ content: [{ type: 'text', text: JSON.stringify(await handleListParameters(), null, 2) }] });
            break;
          case 'get_parameter':
            res.json({ content: [{ type: 'text', text: JSON.stringify(await handleGetParameter(args), null, 2) }] });
            break;
          case 'set_parameter':
            res.json({ content: [{ type: 'text', text: JSON.stringify(await handleSetParameter(args), null, 2) }] });
            break;
          case 'batch_set_parameters':
            res.json({ content: [{ type: 'text', text: JSON.stringify(await handleBatchSetParameters(args), null, 2) }] });
            break;
          default:
            res.status(400).json({ error: `Unknown tool: ${name}` });
        }
      } catch (error: any) {
        res.json({ content: [{ type: 'text', text: JSON.stringify({ error: error.message }, null, 2) }], isError: true });
      }
    });

    this.app.post('/mcp/resources/list', async (req, res) => {
      try {
        res.json({ resources });
      } catch (error: any) {
        res.status(500).json({ error: error.message });
      }
    });

    this.app.post('/mcp/resources/read', async (req, res) => {
      try {
        const result = handleResourceRead(req.body.uri);
        res.json(result);
      } catch (error: any) {
        res.status(500).json({ error: error.message });
      }
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
   * Start the server - uses stdio if spawned by Claude Desktop, HTTP if spawned by C++
   */
  async start(): Promise<void> {
    // Parse port from command line args
    const portArg = process.argv.find(arg => arg.startsWith('--port='));

    if (portArg) {
      // Port specified - use HTTP mode (spawned by C++)
      this.port = parseInt(portArg.split('=')[1], 10);
      this.useStdio = false;

      // Start HTTP server for C++ communication AND MCP SSE endpoints
      await new Promise<void>((resolve, reject) => {
        this.httpServer = this.app.listen(this.port, () => {
          logger.info(`HTTP server listening on port ${this.port}`);
          logger.info(`  - C++ API available at http://localhost:${this.port}/api/*`);
          logger.info(`  - MCP SSE available at http://localhost:${this.port}/sse`);
          resolve();
        }).on('error', reject);
      });

      logger.info('Vital MCP Server ready for connections');
    } else {
      // No port - use stdio mode (spawned by Claude Desktop)
      this.useStdio = true;
      logger.info('Starting MCP Server in stdio mode for Claude Desktop');

      this.transport = new StdioServerTransport();
      await this.server.connect(this.transport);

      logger.info('MCP Server connected via stdio');
    }
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
   * Update the parameter store with new values
   */
  private handleParameterChanged(params: any): void {
    logger.info(`Parameter changed: ${params.name} = ${params.value}`);

    // Update parameter value in store
    const param = parameterStore.getParameter(params.name);
    if (param) {
      param.value = params.value;
      logger.debug(`Updated ${params.name} to ${params.value}`);
    }
  }

  /**
   * Queue a parameter change to be picked up by C++ polling
   */
  public queueParameterChange(name: string, value: number): void {
    this.pendingParameterChanges.push({ name, value });
    logger.info(`Queued parameter change: ${name} = ${value}`);
  }
}

// Export singleton instance for tools to access
export let serverInstance: VitalMcpServer | null = null;
