import { sendBoundCommand } from './auth-session';
import { isOkResponse } from './protocol';

export interface EnhanceStep {
  id: string;
  type: string;
  provider: string;
  enabled: boolean;
  on_error: string;
  params: Record<string, unknown>;
}

export interface EnhancePipeline {
  version: string;
  steps: EnhanceStep[];
  target: {
    type: string;
    format: string;
    export_type: string;
  };
  options?: {
    keep_intermediate?: boolean;
    include_metadata?: boolean;
  };
}

export interface EnhanceWorkflow {
  workflow_id: string;
  name: string;
  description: string;
  pipeline: EnhancePipeline;
  created_at?: string;
  updated_at?: string;
}

export interface EnhanceCapability {
  type: string;
  title: string;
  description?: string;
  i18n_key?: string;
  localized?: Record<string, {
    title?: string;
    description?: string;
    unavailable_reason?: string;
  }>;
  category: string;
  runtime: string;
  available: boolean;
  unavailable_reason?: string;
  requires_capability?: string;
  defaults?: Record<string, unknown>;
  schema?: Record<string, unknown>;
  order_hint?: number;
}

export interface EnhanceCapabilityProvider {
  provider: string;
  kind: string;
  available: boolean;
  capabilities: EnhanceCapability[];
}

export function createDefaultPipeline(): EnhancePipeline {
  return {
    version: 'image.enhance.pipeline.v1',
    steps: [],
    target: {
      type: 'images',
      format: 'jpg',
      export_type: 'single-page',
    },
    options: {
      keep_intermediate: false,
      include_metadata: true,
    },
  };
}

export async function listEnhanceWorkflows(): Promise<EnhanceWorkflow[]> {
  const response = await sendBoundCommand('image.enhance_workflow_list');
  if (!isOkResponse(response)) {
    throw new Error(response.message || 'failed to list workflows');
  }
  return Array.isArray(response.data.workflows) ? response.data.workflows as EnhanceWorkflow[] : [];
}

export async function saveEnhanceWorkflow(workflow: Partial<EnhanceWorkflow> & { pipeline: EnhancePipeline }): Promise<EnhanceWorkflow> {
  const response = await sendBoundCommand('image.enhance_workflow_save', {
    params: { workflow },
  });
  if (!isOkResponse(response)) {
    throw new Error(response.message || 'failed to save workflow');
  }
  return response.data.workflow as EnhanceWorkflow;
}

export async function deleteEnhanceWorkflow(workflowId: string): Promise<void> {
  const response = await sendBoundCommand('image.enhance_workflow_delete', {
    params: { workflow_id: workflowId },
  });
  if (!isOkResponse(response)) {
    throw new Error(response.message || 'failed to delete workflow');
  }
}

export async function loadEnhanceCapabilities(): Promise<EnhanceCapabilityProvider[]> {
  const response = await sendBoundCommand('image.enhance_capabilities');
  if (!isOkResponse(response)) {
    throw new Error(response.message || 'failed to load image enhance capabilities');
  }
  return Array.isArray(response.data.providers) ? response.data.providers as EnhanceCapabilityProvider[] : [];
}
