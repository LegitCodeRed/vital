#!/usr/bin/env node

/**
 * Vital MCP Server Entry Point
 *
 * This server provides Model Context Protocol (MCP) access to Vital synthesizer
 * parameters. It communicates with the C++ application via stdio using JSON-RPC 2.0.
 */

import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { VitalMcpServer } from './server.js';
import { ServerConfig } from './types.js';

// Parse command line arguments
const args = process.argv.slice(2);
const port = parseInt(args.find(arg => arg.startsWith('--port='))?.split('=')[1] || '3000');
const logLevel = (args.find(arg => arg.startsWith('--log-level='))?.split('=')[1] || 'info') as ServerConfig['log_level'];

const config: ServerConfig = {
  port,
  log_level: logLevel,
  heartbeat_interval_ms: 10000,
};

// Create MCP server
const server = new Server(
  {
    name: 'vital-mcp-server',
    version: '1.0.0',
  },
  {
    capabilities: {
      tools: {},
      resources: {},
    },
  }
);

// Create Vital MCP server instance
const vitalServer = new VitalMcpServer(server, config);

// Set up stdio transport
const transport = new StdioServerTransport();

/**
 * Graceful shutdown handler
 */
async function shutdown(signal: string) {
  console.error(`[MCP Server] Received ${signal}, shutting down gracefully...`);

  try {
    await vitalServer.shutdown();
    await server.close();
    process.exit(0);
  } catch (error) {
    console.error('[MCP Server] Error during shutdown:', error);
    process.exit(1);
  }
}

// Register signal handlers
process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));

// Handle uncaught errors
process.on('uncaughtException', (error) => {
  console.error('[MCP Server] Uncaught exception:', error);
  shutdown('uncaughtException');
});

process.on('unhandledRejection', (reason, promise) => {
  console.error('[MCP Server] Unhandled rejection at:', promise, 'reason:', reason);
  shutdown('unhandledRejection');
});

/**
 * Start the server
 */
async function main() {
  try {
    // Connect server to transport
    await server.connect(transport);

    // Initialize Vital server
    await vitalServer.initialize();

    // Send server_ready notification to C++
    const readyMessage = {
      jsonrpc: '2.0',
      method: 'server_ready',
      params: {
        port: config.port,
        pid: process.pid,
      },
    };
    console.log(JSON.stringify(readyMessage));

    console.error('[MCP Server] Started successfully on port', config.port);
  } catch (error) {
    console.error('[MCP Server] Failed to start:', error);
    process.exit(1);
  }
}

// Start the server
main();
