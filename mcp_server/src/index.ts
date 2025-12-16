/**
 * Vital MCP Server - Entry point
 * Spawned by Vital C++ application, communicates via stdio
 */

import { VitalMcpServer } from './server.js';
import logger from './logger.js';

async function main() {
  logger.info('Starting Vital MCP Server...');
  
  const server = new VitalMcpServer();
  
  // Handle graceful shutdown
  process.on('SIGINT', async () => {
    logger.info('Received SIGINT, shutting down...');
    await server.stop();
    process.exit(0);
  });
  
  process.on('SIGTERM', async () => {
    logger.info('Received SIGTERM, shutting down...');
    await server.stop();
    process.exit(0);
  });
  
  process.on('uncaughtException', (error) => {
    logger.error('Uncaught exception:', error);
    process.exit(1);
  });
  
  process.on('unhandledRejection', (reason, promise) => {
    logger.error('Unhandled rejection at:', promise, 'reason:', reason);
    process.exit(1);
  });
  
  try {
    // Start the MCP server
    await server.start();
    
    // Listen for messages from C++ on stdin
    server.listenForCppMessages();
    
    logger.info('Vital MCP Server is running');
  } catch (error) {
    logger.error('Failed to start server:', error);
    process.exit(1);
  }
}

main();
