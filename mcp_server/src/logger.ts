/**
 * Winston logger configuration for MCP server
 */

import winston from 'winston';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const logger = winston.createLogger({
  level: process.env.LOG_LEVEL || 'info',
  format: winston.format.combine(
    winston.format.timestamp(),
    winston.format.errors({ stack: true }),
    winston.format.json()
  ),
  transports: [
    // Write all logs to file
    new winston.transports.File({
      filename: path.join(__dirname, '../logs/mcp_server.log'),
      maxsize: 10 * 1024 * 1024, // 10 MB
      maxFiles: 5,
      tailable: true
    }),
    // Also log to stderr for debugging (C++ can capture this)
    new winston.transports.Console({
      format: winston.format.combine(
        winston.format.colorize(),
        winston.format.simple()
      ),
      stderrLevels: ['error', 'warn', 'info', 'debug']
    })
  ]
});

export default logger;
