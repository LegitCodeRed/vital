/**
 * MCP tool implementations for Vital parameter control
 */

import { z } from 'zod';
import { McpMessage, SetParameterRequest, BatchSetParametersRequest } from './types.js';
import logger from './logger.js';

// Zod schemas for tool inputs
export const SetParameterSchema = z.object({
  name: z.string().describe('Parameter name'),
  value: z.number().min(0).max(1).describe('Parameter value (0.0-1.0)')
});

export const GetParameterSchema = z.object({
  name: z.string().describe('Parameter name')
});

export const BatchSetParametersSchema = z.object({
  parameters: z.array(z.object({
    name: z.string(),
    value: z.number().min(0).max(1)
  })).describe('Array of parameters to set')
});

/**
 * Send a message to C++ via stdout
 */
function sendToCpp(message: McpMessage): void {
  const json = JSON.stringify(message);
  process.stdout.write(json + '\n');
  logger.debug('Sent to C++:', json);
}

/**
 * List all available parameters
 */
export async function handleListParameters(): Promise<any> {
  logger.info('Handling list_parameters request');
  
  // Send request to C++ and wait for response
  const requestId = Date.now().toString();
  const message: McpMessage = {
    jsonrpc: '2.0',
    method: 'list_parameters',
    params: {},
    id: requestId
  };
  
  sendToCpp(message);
  
  // For now, return a placeholder response
  // In a full implementation, we'd wait for C++ to respond
  return {
    success: true,
    message: 'Request sent to Vital. Parameters will be returned via C++ response.'
  };
}

/**
 * Get a single parameter value
 */
export async function handleGetParameter(params: any): Promise<any> {
  const validated = GetParameterSchema.parse(params);
  logger.info(`Getting parameter: ${validated.name}`);
  
  const requestId = Date.now().toString();
  const message: McpMessage = {
    jsonrpc: '2.0',
    method: 'get_parameter',
    params: { name: validated.name },
    id: requestId
  };
  
  sendToCpp(message);
  
  return {
    success: true,
    message: `Request sent to Vital for parameter: ${validated.name}`
  };
}

/**
 * Set a single parameter value
 */
export async function handleSetParameter(params: any): Promise<any> {
  const validated = SetParameterSchema.parse(params);
  logger.info(`Setting parameter: ${validated.name} = ${validated.value}`);
  
  const requestId = Date.now().toString();
  const message: McpMessage = {
    jsonrpc: '2.0',
    method: 'set_parameter',
    params: {
      name: validated.name,
      value: validated.value
    },
    id: requestId
  };
  
  sendToCpp(message);
  
  return {
    success: true,
    parameter: validated.name,
    value: validated.value
  };
}

/**
 * Set multiple parameters atomically
 */
export async function handleBatchSetParameters(params: any): Promise<any> {
  const validated = BatchSetParametersSchema.parse(params);
  logger.info(`Batch setting ${validated.parameters.length} parameters`);
  
  const requestId = Date.now().toString();
  const message: McpMessage = {
    jsonrpc: '2.0',
    method: 'batch_set_parameters',
    params: {
      parameters: validated.parameters
    },
    id: requestId
  };
  
  sendToCpp(message);
  
  return {
    success: true,
    count: validated.parameters.length
  };
}

// Tool definitions for MCP SDK
export const tools = [
  {
    name: 'list_parameters',
    description: 'List all available Vital synthesizer parameters with their metadata',
    inputSchema: {
      type: 'object',
      properties: {},
      required: []
    }
  },
  {
    name: 'get_parameter',
    description: 'Get the current value of a specific parameter',
    inputSchema: {
      type: 'object',
      properties: {
        name: {
          type: 'string',
          description: 'The parameter name (e.g., "osc_1_level", "filter_cutoff")'
        }
      },
      required: ['name']
    }
  },
  {
    name: 'set_parameter',
    description: 'Set a parameter to a specific value (0.0-1.0)',
    inputSchema: {
      type: 'object',
      properties: {
        name: {
          type: 'string',
          description: 'The parameter name'
        },
        value: {
          type: 'number',
          description: 'The parameter value (0.0-1.0)',
          minimum: 0,
          maximum: 1
        }
      },
      required: ['name', 'value']
    }
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
              value: { type: 'number', minimum: 0, maximum: 1 }
            },
            required: ['name', 'value']
          }
        }
      },
      required: ['parameters']
    }
  }
];
