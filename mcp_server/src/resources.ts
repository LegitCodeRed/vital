/**
 * MCP resource handlers for Vital state and metadata
 */

import logger from './logger.js';

/**
 * Resource definitions for MCP SDK
 */
export const resources = [
  {
    uri: 'vital://parameter_schema',
    name: 'Vital Parameter Schema',
    description: 'Complete metadata for all Vital parameters including ranges, categories, and display names',
    mimeType: 'application/json'
  },
  {
    uri: 'vital://current_state',
    name: 'Current Synth State',
    description: 'Current values of all parameters and modulations',
    mimeType: 'application/json'
  }
];

/**
 * Handle resource requests
 */
export async function handleResourceRead(uri: string): Promise<any> {
  logger.info(`Reading resource: ${uri}`);
  
  switch (uri) {
    case 'vital://parameter_schema':
      return {
        contents: [{
          uri,
          mimeType: 'application/json',
          text: JSON.stringify({
            message: 'Parameter schema will be provided by C++ Vital application',
            note: 'Use list_parameters tool to get actual parameter data'
          })
        }]
      };
      
    case 'vital://current_state':
      return {
        contents: [{
          uri,
          mimeType: 'application/json',
          text: JSON.stringify({
            message: 'Current state will be provided by C++ Vital application',
            note: 'Use get_parameter tool to query specific parameters'
          })
        }]
      };
      
    default:
      throw new Error(`Unknown resource URI: ${uri}`);
  }
}
