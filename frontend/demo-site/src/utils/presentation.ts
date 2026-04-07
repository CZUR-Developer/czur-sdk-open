import type { CapabilityStatus, ExecutionState, RequirementTag, Tone } from '../types/demo';

export function executionStateLabelKey(state: ExecutionState): string {
  switch (state) {
    case 'running':
      return 'status.running';
    case 'success':
      return 'status.success';
    case 'error':
      return 'status.error';
    case 'blocked':
      return 'status.blocked';
    case 'planned':
      return 'status.planned';
    default:
      return 'status.idle';
  }
}

export function executionStateTone(state: ExecutionState): Tone {
  switch (state) {
    case 'running':
      return 'primary';
    case 'success':
      return 'success';
    case 'error':
      return 'danger';
    case 'blocked':
      return 'warning';
    case 'planned':
      return 'neutral';
    default:
      return 'info';
  }
}

export function capabilityStatusLabelKey(status: CapabilityStatus): string {
  switch (status) {
    case 'ga':
      return 'status.ga';
    case 'beta':
      return 'status.beta';
    default:
      return 'status.planned';
  }
}

export function capabilityStatusTone(status: CapabilityStatus): Tone {
  switch (status) {
    case 'ga':
      return 'success';
    case 'beta':
      return 'warning';
    default:
      return 'neutral';
  }
}

export function requirementTagLabelKey(tag: RequirementTag): string {
  switch (tag) {
    case 'device':
      return 'tags.device';
    case 'provider':
      return 'tags.provider';
    default:
      return 'tags.account';
  }
}

export function requirementTagTone(tag: RequirementTag): Tone {
  switch (tag) {
    case 'device':
      return 'info';
    case 'provider':
      return 'warning';
    default:
      return 'primary';
  }
}
