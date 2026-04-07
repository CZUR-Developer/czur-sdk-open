import type {
  ConfigLine,
  EntryCardItem,
  InfoCardItem,
  MetricCardItem,
  NavigationItem,
  TimelineItem,
} from '../types/admin';

export const navigationItems: NavigationItem[] = [
  { key: 'dashboard', to: '/dashboard', labelKey: 'nav.dashboard' },
  { key: 'websocket', to: '/websocket', labelKey: 'nav.websocket' },
  { key: 'system', to: '/system', labelKey: 'nav.system' },
  { key: 'auth', to: '/auth', labelKey: 'nav.auth' },
  { key: 'log', to: '/log', labelKey: 'nav.log' },
  { key: 'config', to: '/config', labelKey: 'nav.config' },
];

export const supportNavigationItems: NavigationItem[] = [
  {
    key: 'support',
    href: 'https://www.czur.com/support',
    external: true,
    labelKey: 'nav.support',
  },
  {
    key: 'documention',
    href: 'https://www.czur.com/support',
    external: true,
    labelKey: 'nav.documention',
  },
];

export const dashboardMetrics: MetricCardItem[] = [
  {
    id: 'availability',
    value: '99.982%',
    labelKey: 'metrics.availability',
    detailKey: 'metrics.availabilityDetail',
    trend: '+0.18%',
    tone: 'success',
  },
  {
    id: 'active-users',
    value: '1,248',
    labelKey: 'metrics.activeSockets',
    detailKey: 'metrics.activeSocketsDetail',
    trend: '+84',
    tone: 'primary',
  },
  {
    id: 'config',
    value: '12',
    labelKey: 'metrics.configProfiles',
    detailKey: 'metrics.configProfilesDetail',
    trend: '+1 snapshot',
    tone: 'neutral',
  },
  {
    id: 'throughput',
    value: '428/s',
    labelKey: 'metrics.eventThroughput',
    detailKey: 'metrics.eventThroughputDetail',
    trend: '+11%',
    tone: 'warning',
  },
];

export const dashboardEntries: EntryCardItem[] = [
  {
    key: 'websocket',
    eyebrowKey: 'cards.websocketEntryEyebrow',
    titleKey: 'nav.websocket',
    descriptionKey: 'cards.websocketEntryDescription',
    to: '/websocket',
    ctaKey: 'common.openModule',
    stats: [
      { labelKey: 'common.activeRoutes', value: '14' },
      { labelKey: 'common.serviceHealth', value: '99.7%' },
    ],
  },
  {
    key: 'system',
    eyebrowKey: 'cards.systemEntryEyebrow',
    titleKey: 'nav.system',
    descriptionKey: 'cards.systemEntryDescription',
    to: '/system',
    ctaKey: 'common.reviewConfig',
    stats: [
      { labelKey: 'common.configSnapshots', value: '12' },
      { labelKey: 'common.liveSignals', value: '27' },
    ],
  },
  {
    key: 'auth',
    eyebrowKey: 'cards.authEntryEyebrow',
    titleKey: 'nav.auth',
    descriptionKey: 'cards.authEntryDescription',
    to: '/auth',
    ctaKey: 'common.openModule',
    stats: [
      { labelKey: 'common.serviceHealth', value: '99.99%' },
      { labelKey: 'common.activeRoutes', value: '06' },
    ],
  },
  {
    key: 'log',
    eyebrowKey: 'cards.logEntryEyebrow',
    titleKey: 'nav.log',
    descriptionKey: 'cards.logEntryDescription',
    to: '/log',
    ctaKey: 'common.openModule',
    stats: [
      { labelKey: 'common.liveSignals', value: '312' },
      { labelKey: 'common.serviceHealth', value: 'Healthy' },
    ],
  },
  {
    key: 'config',
    eyebrowKey: 'cards.configEntryEyebrow',
    titleKey: 'nav.config',
    descriptionKey: 'cards.configEntryDescription',
    to: '/config',
    ctaKey: 'common.reviewConfig',
    stats: [
      { labelKey: 'common.configSnapshots', value: '12' },
      { labelKey: 'common.activeRoutes', value: '07' },
    ],
  },
];

export const dashboardHighlights: TimelineItem[] = [
  {
    id: 'northstar',
    titleKey: 'cards.runtimeNorthstarTitle',
    detailKey: 'cards.runtimeNorthstarDetail',
    meta: '20:14 CST',
    tone: 'primary',
  },
  {
    id: 'config',
    titleKey: 'cards.configRefactorTitle',
    detailKey: 'cards.configRefactorDetail',
    meta: '18:40 CST',
    tone: 'success',
  },
  {
    id: 'websocket',
    titleKey: 'cards.websocketTitle',
    detailKey: 'cards.websocketDetail',
    meta: '16:05 CST',
    tone: 'warning',
  },
];

export const dashboardMilestones: TimelineItem[] = [
  {
    id: 'auth',
    titleKey: 'cards.authMilestoneTitle',
    detailKey: 'cards.authMilestoneDetail',
    meta: 'Release r2026.04',
    tone: 'success',
  },
  {
    id: 'rollout',
    titleKey: 'cards.rolloutTitle',
    detailKey: 'cards.rolloutDetail',
    meta: 'New shell baseline',
    tone: 'primary',
  },
];

export const systemMetrics: MetricCardItem[] = [
  {
    id: 'version',
    value: 'v0.1.0',
    labelKey: 'metrics.runtimeVersion',
    detailKey: 'metrics.runtimeVersionDetail',
    trend: 'build 2026.04.07',
    tone: 'primary',
  },
  {
    id: 'bundles',
    value: '07',
    labelKey: 'metrics.configBundles',
    detailKey: 'metrics.configBundlesDetail',
    trend: '+1 env',
    tone: 'neutral',
  },
  {
    id: 'flags',
    value: '18',
    labelKey: 'metrics.featureFlags',
    detailKey: 'metrics.featureFlagsDetail',
    trend: '3 staged',
    tone: 'warning',
  },
];

export const systemConfig: ConfigLine[] = [
  {
    labelKey: 'header.cluster',
    value: 'prod-sh-01',
    hintKey: 'cards.configHotReload',
  },
  {
    labelKey: 'header.releaseTrack',
    value: 'GA / blue-green',
    hintKey: 'cards.configGuardrails',
  },
  {
    labelKey: 'common.configSnapshots',
    value: 'runtime.base -> runtime.prod -> workspace.admin',
    hintKey: 'cards.configSecrets',
  },
];

export const systemChecklist: TimelineItem[] = [
  {
    id: 'check-1',
    titleKey: 'cards.rolloutStepOne',
    detailKey: 'cards.configRefactorDetail',
    meta: 'UI guardrail',
    tone: 'primary',
  },
  {
    id: 'check-2',
    titleKey: 'cards.rolloutStepTwo',
    detailKey: 'cards.runtimeNorthstarDetail',
    meta: 'Typography',
    tone: 'neutral',
  },
  {
    id: 'check-3',
    titleKey: 'cards.rolloutStepThree',
    detailKey: 'cards.rolloutDetail',
    meta: 'API-ready slot',
    tone: 'warning',
  },
];

export const websocketMetrics: MetricCardItem[] = [
  {
    id: 'sockets',
    value: '1,248',
    labelKey: 'metrics.activeSockets',
    detailKey: 'metrics.activeSocketsDetail',
    trend: '+84',
    tone: 'primary',
  },
  {
    id: 'throughput',
    value: '428/s',
    labelKey: 'metrics.eventThroughput',
    detailKey: 'metrics.eventThroughputDetail',
    trend: '+11%',
    tone: 'success',
  },
  {
    id: 'heartbeat',
    value: '99.94%',
    labelKey: 'metrics.heartbeatHealth',
    detailKey: 'metrics.heartbeatHealthDetail',
    trend: '2 retries',
    tone: 'warning',
  },
];

export const websocketChannels: InfoCardItem[] = [
  {
    id: 'ops',
    eyebrowKey: 'common.stable',
    title: 'ops.broadcast',
    descriptionKey: 'cards.pipelineBroadcastDetail',
    meta: 'Subscribers 412',
    badgeKey: 'common.stable',
    tone: 'primary',
  },
  {
    id: 'workspace',
    eyebrowKey: 'common.stable',
    title: 'workspace.delta',
    descriptionKey: 'cards.pipelineIngressDetail',
    meta: 'Subscribers 531',
    badgeKey: 'common.stable',
    tone: 'success',
  },
  {
    id: 'heartbeat',
    eyebrowKey: 'common.warning',
    title: 'heartbeat.guard',
    descriptionKey: 'cards.pipelineAckDetail',
    meta: 'Ack p95 182ms',
    badgeKey: 'common.warning',
    tone: 'warning',
  },
];

export const websocketPipeline: TimelineItem[] = [
  {
    id: 'ingress',
    titleKey: 'cards.pipelineIngressTitle',
    detailKey: 'cards.pipelineIngressDetail',
    meta: 'Gateway -> command bus',
    tone: 'primary',
  },
  {
    id: 'broadcast',
    titleKey: 'cards.pipelineBroadcastTitle',
    detailKey: 'cards.pipelineBroadcastDetail',
    meta: 'Topic fan-out',
    tone: 'success',
  },
  {
    id: 'ack',
    titleKey: 'cards.pipelineAckTitle',
    detailKey: 'cards.pipelineAckDetail',
    meta: 'Delivery confirm',
    tone: 'warning',
  },
];

export const websocketIncidents: TimelineItem[] = [
  {
    id: 'incident-1',
    titleKey: 'cards.incidentTitleOne',
    detailKey: 'cards.incidentDetailOne',
    meta: 'Severity S2',
    tone: 'warning',
  },
  {
    id: 'incident-2',
    titleKey: 'cards.incidentTitleTwo',
    detailKey: 'cards.incidentDetailTwo',
    meta: 'Severity S3',
    tone: 'success',
  },
];

export const authMetrics: MetricCardItem[] = [
  {
    id: 'sessions',
    value: '3,904',
    labelKey: 'metrics.authSessions',
    detailKey: 'metrics.authSessionsDetail',
    trend: '+2.4%',
    tone: 'primary',
  },
  {
    id: 'providers',
    value: '06',
    labelKey: 'metrics.authProviders',
    detailKey: 'metrics.authProvidersDetail',
    trend: '2 SSO',
    tone: 'success',
  },
  {
    id: 'failures',
    value: '0.03%',
    labelKey: 'metrics.authFailures',
    detailKey: 'metrics.authFailuresDetail',
    trend: '-0.01%',
    tone: 'warning',
  },
];

export const authCards: InfoCardItem[] = [
  {
    id: 'sso',
    eyebrowKey: 'common.stable',
    title: 'SSO Gateways',
    descriptionKey: 'cards.authSsoDescription',
    meta: 'OIDC · SAML · Enterprise',
    badgeKey: 'common.stable',
    tone: 'primary',
  },
  {
    id: 'tokens',
    eyebrowKey: 'common.stable',
    title: 'Token Policies',
    descriptionKey: 'cards.authTokenDescription',
    meta: 'JWT · refresh · device binding',
    badgeKey: 'common.stable',
    tone: 'success',
  },
  {
    id: 'guard',
    eyebrowKey: 'common.warning',
    title: 'Risk Controls',
    descriptionKey: 'cards.authRiskDescription',
    meta: 'IP fencing · replay guard',
    badgeKey: 'common.warning',
    tone: 'warning',
  },
];

export const authTimeline: TimelineItem[] = [
  {
    id: 'auth-1',
    titleKey: 'cards.authMilestoneTitle',
    detailKey: 'cards.authMilestoneDetail',
    meta: 'MFA coverage 100%',
    tone: 'success',
  },
  {
    id: 'auth-2',
    titleKey: 'cards.authPlaceholderTitle',
    detailKey: 'cards.authPlaceholderDetail',
    meta: 'Placeholder module',
    tone: 'primary',
  },
];

export const logMetrics: MetricCardItem[] = [
  {
    id: 'ingest',
    value: '14.2M',
    labelKey: 'metrics.logIngest',
    detailKey: 'metrics.logIngestDetail',
    trend: '+6%',
    tone: 'primary',
  },
  {
    id: 'retention',
    value: '30d',
    labelKey: 'metrics.logRetention',
    detailKey: 'metrics.logRetentionDetail',
    trend: 'Hot + cold',
    tone: 'neutral',
  },
  {
    id: 'alerts',
    value: '03',
    labelKey: 'metrics.logAlerts',
    detailKey: 'metrics.logAlertsDetail',
    trend: '1 active',
    tone: 'warning',
  },
];

export const logCards: InfoCardItem[] = [
  {
    id: 'stream',
    eyebrowKey: 'common.stable',
    title: 'Live Streams',
    descriptionKey: 'cards.logStreamsDescription',
    meta: 'runtime · auth · transport',
    badgeKey: 'common.stable',
    tone: 'primary',
  },
  {
    id: 'search',
    eyebrowKey: 'common.stable',
    title: 'Search Profiles',
    descriptionKey: 'cards.logSearchDescription',
    meta: 'saved queries · facets',
    badgeKey: 'common.stable',
    tone: 'success',
  },
  {
    id: 'watch',
    eyebrowKey: 'common.warning',
    title: 'Alert Watchers',
    descriptionKey: 'cards.logWatchDescription',
    meta: 'thresholds · anomaly rules',
    badgeKey: 'common.warning',
    tone: 'warning',
  },
];

export const logTimeline: TimelineItem[] = [
  {
    id: 'log-1',
    titleKey: 'cards.logMilestoneTitle',
    detailKey: 'cards.logMilestoneDetail',
    meta: 'Retention policy synced',
    tone: 'success',
  },
  {
    id: 'log-2',
    titleKey: 'cards.logPlaceholderTitle',
    detailKey: 'cards.logPlaceholderDetail',
    meta: 'Placeholder module',
    tone: 'primary',
  },
];

export const configMetricsPage: MetricCardItem[] = [
  {
    id: 'bundles',
    value: '07',
    labelKey: 'metrics.configBundles',
    detailKey: 'metrics.configBundlesDetail',
    trend: '+1 env',
    tone: 'primary',
  },
  {
    id: 'profiles',
    value: '12',
    labelKey: 'metrics.configProfiles',
    detailKey: 'metrics.configProfilesDetail',
    trend: 'Latest synced',
    tone: 'success',
  },
  {
    id: 'flags',
    value: '18',
    labelKey: 'metrics.featureFlags',
    detailKey: 'metrics.featureFlagsDetail',
    trend: '3 staged',
    tone: 'warning',
  },
];

export const configCards: InfoCardItem[] = [
  {
    id: 'env',
    eyebrowKey: 'common.stable',
    title: 'Environment Sets',
    descriptionKey: 'cards.configEnvDescription',
    meta: 'local · staging · prod',
    badgeKey: 'common.stable',
    tone: 'primary',
  },
  {
    id: 'feature',
    eyebrowKey: 'common.stable',
    title: 'Feature Switches',
    descriptionKey: 'cards.configFeatureDescription',
    meta: 'typed flags · scoped rollout',
    badgeKey: 'common.stable',
    tone: 'success',
  },
  {
    id: 'secrets',
    eyebrowKey: 'common.warning',
    title: 'Secret References',
    descriptionKey: 'cards.configSecretDescription',
    meta: 'masked placeholders',
    badgeKey: 'common.warning',
    tone: 'warning',
  },
];

export const configTimeline: TimelineItem[] = [
  {
    id: 'cfg-1',
    titleKey: 'cards.configMilestoneTitle',
    detailKey: 'cards.configMilestoneDetail',
    meta: 'Visual grouping aligned',
    tone: 'success',
  },
  {
    id: 'cfg-2',
    titleKey: 'cards.configPlaceholderTitle',
    detailKey: 'cards.configPlaceholderDetail',
    meta: 'Placeholder module',
    tone: 'primary',
  },
];

export const supportMetrics: MetricCardItem[] = [
  {
    id: 'tickets',
    value: '24',
    labelKey: 'metrics.supportTickets',
    detailKey: 'metrics.supportTicketsDetail',
    trend: '5 pending',
    tone: 'primary',
  },
  {
    id: 'sla',
    value: '98.6%',
    labelKey: 'metrics.supportSla',
    detailKey: 'metrics.supportSlaDetail',
    trend: '+0.4%',
    tone: 'success',
  },
  {
    id: 'channels',
    value: '03',
    labelKey: 'metrics.supportChannels',
    detailKey: 'metrics.supportChannelsDetail',
    trend: 'Email · Chat · Ticket',
    tone: 'warning',
  },
];

export const supportCards: InfoCardItem[] = [
  {
    id: 'queue',
    eyebrowKey: 'common.stable',
    title: 'Request Queue',
    descriptionKey: 'cards.supportQueueDescription',
    meta: 'triage · assignment · follow-up',
    badgeKey: 'common.stable',
    tone: 'primary',
  },
  {
    id: 'status',
    eyebrowKey: 'common.stable',
    title: 'Service Status',
    descriptionKey: 'cards.supportStatusDescription',
    meta: 'uptime · incidents · maintenance',
    badgeKey: 'common.stable',
    tone: 'success',
  },
  {
    id: 'escalation',
    eyebrowKey: 'common.warning',
    title: 'Escalations',
    descriptionKey: 'cards.supportEscalationDescription',
    meta: 'priority routing',
    badgeKey: 'common.warning',
    tone: 'warning',
  },
];

export const supportTimeline: TimelineItem[] = [
  {
    id: 'support-1',
    titleKey: 'cards.supportMilestoneTitle',
    detailKey: 'cards.supportMilestoneDetail',
    meta: 'Queue synced',
    tone: 'success',
  },
  {
    id: 'support-2',
    titleKey: 'cards.supportPlaceholderTitle',
    detailKey: 'cards.supportPlaceholderDetail',
    meta: 'Placeholder module',
    tone: 'primary',
  },
];

export const documentionMetrics: MetricCardItem[] = [
  {
    id: 'docs',
    value: '128',
    labelKey: 'metrics.docsPages',
    detailKey: 'metrics.docsPagesDetail',
    trend: '+6 updated',
    tone: 'primary',
  },
  {
    id: 'coverage',
    value: '91%',
    labelKey: 'metrics.docsCoverage',
    detailKey: 'metrics.docsCoverageDetail',
    trend: '+3%',
    tone: 'success',
  },
  {
    id: 'searches',
    value: '2.3K',
    labelKey: 'metrics.docsSearches',
    detailKey: 'metrics.docsSearchesDetail',
    trend: 'weekly',
    tone: 'warning',
  },
];

export const documentionCards: InfoCardItem[] = [
  {
    id: 'guides',
    eyebrowKey: 'common.stable',
    title: 'Guides',
    descriptionKey: 'cards.docsGuidesDescription',
    meta: 'setup · usage · troubleshooting',
    badgeKey: 'common.stable',
    tone: 'primary',
  },
  {
    id: 'api',
    eyebrowKey: 'common.stable',
    title: 'API Notes',
    descriptionKey: 'cards.docsApiDescription',
    meta: 'endpoints · examples · types',
    badgeKey: 'common.stable',
    tone: 'success',
  },
  {
    id: 'release',
    eyebrowKey: 'common.warning',
    title: 'Release Notes',
    descriptionKey: 'cards.docsReleaseDescription',
    meta: 'changes · migrations',
    badgeKey: 'common.warning',
    tone: 'warning',
  },
];

export const documentionTimeline: TimelineItem[] = [
  {
    id: 'docs-1',
    titleKey: 'cards.docsMilestoneTitle',
    detailKey: 'cards.docsMilestoneDetail',
    meta: 'Knowledge base refreshed',
    tone: 'success',
  },
  {
    id: 'docs-2',
    titleKey: 'cards.docsPlaceholderTitle',
    detailKey: 'cards.docsPlaceholderDetail',
    meta: 'Placeholder module',
    tone: 'primary',
  },
];
