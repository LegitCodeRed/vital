/**
 * MCP tool implementations for Vital parameter control
 */

import { z } from 'zod';
import { McpMessage, SetParameterRequest, BatchSetParametersRequest } from './types.js';
import logger from './logger.js';
import { parameterStore } from './parameter-store.js';
import fs from 'fs/promises';
import path from 'path';

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

export const ImportWavetableSchema = z.object({
  fileName: z.string().min(1).describe('Name for the saved wavetable file (will be sanitized)'),
  data: z.string().min(1).describe('Base64-encoded .vitaltable contents'),
  oscillator: z.number().int().min(1).max(3).default(1).describe('Target oscillator slot (1-3)'),
  description: z.string().optional().describe('Optional description for logging/debugging')
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

  const parameters = parameterStore.getAllParameters();

  if (parameters.length === 0) {
    return {
      success: false,
      message: 'No parameters loaded yet. Make sure Vital MCP bridge is connected and metadata has been sent.',
      parameter_count: 0
    };
  }

  return {
    success: true,
    parameter_count: parameters.length,
    parameters: parameters.map(p => ({
      name: p.name,
      display_name: p.display_name,
      value: p.value,
      min: p.min,
      max: p.max,
      default: p.default,
      units: p.units,
      string_lookup: p.string_lookup
    }))
  };
}

/**
 * Get a single parameter value
 */
export async function handleGetParameter(params: any): Promise<any> {
  const validated = GetParameterSchema.parse(params);
  logger.info(`Getting parameter: ${validated.name}`);

  const parameter = parameterStore.getParameter(validated.name);

  if (!parameter) {
    return {
      success: false,
      message: `Parameter '${validated.name}' not found. Available parameters: ${parameterStore.getParameterCount()}`
    };
  }

  return {
    success: true,
    parameter: {
      name: parameter.name,
      display_name: parameter.display_name,
      value: parameter.value,
      min: parameter.min,
      max: parameter.max,
      units: parameter.units,
      string_lookup: parameter.string_lookup
    }
  };
}

/**
 * Set a single parameter value
 */
export async function handleSetParameter(params: any): Promise<any> {
  const validated = SetParameterSchema.parse(params);
  logger.info(`Setting parameter: ${validated.name} = ${validated.value}`);

  // Check if parameter exists
  const parameter = parameterStore.getParameter(validated.name);
  if (!parameter) {
    return {
      success: false,
      message: `Parameter '${validated.name}' not found`
    };
  }

  // Queue the change for C++ to pick up
  const server = (globalThis as any).vitalServer;
  if (server) {
    server.queueParameterChange(validated.name, validated.value);
  }

  return {
    success: true,
    message: `Queued ${validated.name} = ${validated.value}`,
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

  const server = (globalThis as any).vitalServer;
  if (!server) {
    return {
      success: false,
      message: 'Server instance not available'
    };
  }

  const results = [];
  for (const param of validated.parameters) {
    const parameter = parameterStore.getParameter(param.name);
    if (parameter) {
      server.queueParameterChange(param.name, param.value);
      results.push({ name: param.name, value: param.value, queued: true });
    } else {
      results.push({ name: param.name, value: param.value, queued: false, error: 'Not found' });
    }
  }

  return {
    success: true,
    message: `Queued ${results.filter(r => r.queued).length} of ${validated.parameters.length} parameters`,
    count: validated.parameters.length,
    results
  };
}

/**
 * Store a .vitaltable and queue it for import into the running synth
 */
export async function handleImportWavetable(params: any): Promise<any> {
  const validated = ImportWavetableSchema.parse(params);
  const server = (globalThis as any).vitalServer;

  if (!server) {
    return {
      success: false,
      message: 'Server instance not available - cannot queue wavetable import'
    };
  }

  const safeName = validated.fileName.replace(/[^a-zA-Z0-9._-]/g, '_');
  const finalName = safeName.toLowerCase().endsWith('.vitaltable') ? safeName : `${safeName}.vitaltable`;

  const wavetableDir = path.join(process.cwd(), 'wavetables');
  await fs.mkdir(wavetableDir, { recursive: true });

  const buffer = Buffer.from(validated.data, 'base64');
  const maxBytes = 10 * 1024 * 1024; // 10 MB guardrail
  if (buffer.length === 0) {
    throw new Error('Wavetable payload is empty');
  }
  if (buffer.length > maxBytes) {
    throw new Error(`Wavetable too large (${buffer.length} bytes). Limit is ${maxBytes} bytes.`);
  }

  const fullPath = path.join(wavetableDir, finalName);
  await fs.writeFile(fullPath, buffer);

  if (typeof server.queueWavetableImport === 'function') {
    server.queueWavetableImport({
      oscillator: validated.oscillator,
      path: fullPath,
      name: finalName,
      description: validated.description
    });
  }

  logger.info(`Saved wavetable ${finalName} (${buffer.length} bytes) for oscillator ${validated.oscillator}`);

  return {
    success: true,
    message: `Wavetable saved and queued for import`,
    file: fullPath,
    oscillator: validated.oscillator
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
  },
  {
    name: 'import_wavetable',
    description: 'Upload a .vitaltable (base64) and queue it for automatic import into Vital',
    inputSchema: {
      type: 'object',
      properties: {
        fileName: { type: 'string', description: 'Filename to store (will be sanitized and .vitaltable enforced)' },
        data: { type: 'string', description: 'Base64-encoded .vitaltable contents' },
        oscillator: { type: 'integer', minimum: 1, maximum: 3, description: 'Target oscillator slot (1-3)' },
        description: { type: 'string', description: 'Optional label for logs' }
      },
      required: ['fileName', 'data']
    }
  }
];
