/**
 * Parameter metadata storage
 * Stores parameter information received from Vital C++
 */

export interface ParameterMetadata {
  name: string;
  display_name: string;
  value: number;
  min: number;
  max: number;
  default: number;
  units?: string;
  string_lookup?: string[];
  version_added?: number;
  is_automation_parameter: boolean;
}

class ParameterStore {
  private parameters: Map<string, ParameterMetadata> = new Map();

  /**
   * Store parameter metadata from C++
   */
  setParameters(params: ParameterMetadata[]): void {
    this.parameters.clear();
    for (const param of params) {
      this.parameters.set(param.name, param);
    }
  }

  /**
   * Get all parameters
   */
  getAllParameters(): ParameterMetadata[] {
    return Array.from(this.parameters.values());
  }

  /**
   * Get a single parameter by name
   */
  getParameter(name: string): ParameterMetadata | undefined {
    return this.parameters.get(name);
  }

  /**
   * Check if parameters have been loaded
   */
  hasParameters(): boolean {
    return this.parameters.size > 0;
  }

  /**
   * Get parameter count
   */
  getParameterCount(): number {
    return this.parameters.size;
  }
}

export const parameterStore = new ParameterStore();
