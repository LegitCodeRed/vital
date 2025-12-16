#!/usr/bin/env node

/**
 * Vital MCP Server Entry Point (HTTP/REST version)
 *
 * This server provides HTTP REST API access to Vital synthesizer parameters.
 * It also hosts MCP endpoints for Claude Desktop integration.
 */

import express from 'express';
import cors from 'cors';
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

// Create Express app
const app = express();

// Middleware
app.use(cors());
app.use(express.json());

// Create Vital MCP server instance
const vitalServer = new VitalMcpServer(config);

// Health check endpoint
app.get('/health', (req, res) => {
  res.json({
    status: 'ok',
    version: '1.0.0',
    uptime: process.uptime(),
  });
});

// Parameter endpoints
app.get('/api/parameters', async (req, res) => {
  try {
    const params = vitalServer.listParameters();
    res.json({ success: true, parameters: params });
  } catch (error) {
    res.status(500).json({ success: false, error: String(error) });
  }
});

app.get('/api/parameters/:name', async (req, res) => {
  try {
    const param = vitalServer.getParameter(req.params.name);
    res.json({ success: true, parameter: param });
  } catch (error) {
    res.status(404).json({ success: false, error: String(error) });
  }
});

app.post('/api/parameters/:name', async (req, res) => {
  try {
    const { value } = req.body;
    vitalServer.setParameter(req.params.name, value);
    res.json({ success: true });
  } catch (error) {
    res.status(400).json({ success: false, error: String(error) });
  }
});

app.post('/api/parameters', async (req, res) => {
  try {
    const { parameters } = req.body;
    vitalServer.batchSetParameters(parameters);
    res.json({ success: true });
  } catch (error) {
    res.status(400).json({ success: false, error: String(error) });
  }
});

// Parameter update notification from C++ (Vital sends parameter changes here)
app.post('/api/notify/parameter_changed', (req, res) => {
  try {
    const { name, value } = req.body;
    vitalServer.onParameterChanged(name, value);
    res.json({ success: true });
  } catch (error) {
    res.status(400).json({ success: false, error: String(error) });
  }
});

// Receive parameter metadata from C++
app.post('/api/metadata/parameters', (req, res) => {
  try {
    const { parameters } = req.body;
    vitalServer.updateParameterMetadata(parameters);
    res.json({ success: true });
  } catch (error) {
    res.status(400).json({ success: false, error: String(error) });
  }
});

// Return pending parameter change requests to C++
app.get('/api/pending_changes', (req, res) => {
  try {
    const pending = vitalServer.getPendingChanges();
    res.json({ parameters: pending });
  } catch (error) {
    res.status(500).json({ parameters: [] });
  }
});

// Heartbeat endpoint
app.post('/api/heartbeat', (req, res) => {
  res.json({ success: true, timestamp: Date.now() });
});

/**
 * Graceful shutdown handler
 */
async function shutdown(signal: string) {
  console.log(`[MCP Server] Received ${signal}, shutting down gracefully...`);

  server.close(() => {
    console.log('[MCP Server] HTTP server closed');
    process.exit(0);
  });

  // Force close after 5 seconds
  setTimeout(() => {
    console.error('[MCP Server] Forced shutdown after timeout');
    process.exit(1);
  }, 5000);
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
});

/**
 * Start the server
 */
const server = app.listen(port, () => {
  console.log(`[MCP Server] HTTP server listening on http://localhost:${port}`);
  console.log(`[MCP Server] Health check: http://localhost:${port}/health`);
  console.log(`[MCP Server] API endpoints: http://localhost:${port}/api/parameters`);
});
