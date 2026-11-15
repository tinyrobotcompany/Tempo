#!/usr/bin/env node

import fs from 'fs';
import process from 'process';
import {
  buildSystemPrompt,
  buildUserPrompt,
  buildOpenAiErrorDiagnostics,
  buildOpenAiReviewRequestPayload,
  buildOpenAiResponseDiagnostics,
  extractReviewBodyFromOpenAiPayload,
  isStructurallyValidReviewBody,
  loadSkillRubrics,
} from './codex-pr-review-core.mjs';

const eventPath = process.env.GITHUB_EVENT_PATH;
if (!eventPath) {
  console.error('GITHUB_EVENT_PATH is not set.');
  process.exit(1);
}

const event = JSON.parse(fs.readFileSync(eventPath, 'utf8'));
const pr = event.pull_request;
if (!pr) {
  console.error('This workflow must run on a pull_request or pull_request_target event.');
  process.exit(1);
}

const repoSlug = process.env.GITHUB_REPOSITORY || '';
const [owner, repo] = repoSlug.split('/');
if (!/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repoSlug)) {
  console.error('GITHUB_REPOSITORY is not set or invalid.');
  process.exit(1);
}

const githubToken = process.env.GITHUB_TOKEN;
if (!githubToken) {
  console.error('GITHUB_TOKEN is not set.');
  process.exit(1);
}

const openAiKey = process.env.OPENAI_API_KEY;
if (!openAiKey) {
  console.error('OPENAI_API_KEY is not set.');
  process.exit(1);
}

// Credentials may only go to the intended GitHub service; never follow redirects.
const githubApi = 'https://api.github.com';
if (process.env.GITHUB_API_URL && process.env.GITHUB_API_URL !== githubApi) {
  throw new Error('Unsupported GitHub API origin');
}
if (!Number.isSafeInteger(pr.number) || pr.number <= 0 ||
    !/^[a-f0-9]{40}$/.test(pr.head?.sha || '') || !/^[a-f0-9]{40}$/.test(pr.base?.sha || '')) {
  throw new Error('Invalid PR identity');
}
const model = process.env.CODEX_REVIEW_MODEL || 'gpt-5.2';
const maxDiffChars = Number.parseInt(process.env.CODEX_REVIEW_DIFF_MAX || '120000', 10);
const maxOutputTokens = Number.parseInt(process.env.CODEX_REVIEW_MAX_OUTPUT_TOKENS || '6000', 10);
const reasoningEffort = process.env.CODEX_REVIEW_REASONING_EFFORT || 'low';
const codexReviewMarker = `<!-- codex-review:${pr.head?.sha || process.env.GITHUB_SHA || 'unknown'} -->`;

async function githubRequest(path, options = {}) {
  const response = await fetch(`${githubApi}${path}`, {
    ...options,
    redirect: 'error',
    signal: AbortSignal.timeout(60000),
    headers: {
      Authorization: `Bearer ${githubToken}`,
      'User-Agent': 'codex-pr-review',
      ...options.headers,
    },
  });

  if (!response.ok) {
    const text = await response.text();
    throw new Error(`GitHub API ${response.status} ${response.statusText}: ${text}`);
  }

  return response;
}

async function assertCurrentHead() {
  const response = await githubRequest(`/repos/${owner}/${repo}/pulls/${pr.number}`, {headers:{Accept:'application/vnd.github+json'}});
  const current = await response.json();
  if (!pr.head?.sha || current.head?.sha !== pr.head.sha || current.base?.sha !== pr.base?.sha) {
    throw new Error('PR changed during review; rerun for the current head and base.');
  }
}
await assertCurrentHead();

const diffResponse = await githubRequest(`/repos/${owner}/${repo}/pulls/${pr.number}`,
  {
    headers: {
      Accept: 'application/vnd.github.v3.diff',
    },
  });

let diff = await diffResponse.text();
let diffTruncated = false;
if (diff.length > maxDiffChars) {
  throw new Error('PR diff exceeds the review limit. Split the PR; partial review cannot pass.');
}

const rubrics = loadSkillRubrics(process.cwd());
const systemPrompt = buildSystemPrompt(rubrics);
const userPrompt = buildUserPrompt(pr, diff, maxDiffChars, diffTruncated);

const openAiResponse = await fetch('https://api.openai.com/v1/responses', {
  method: 'POST',
  redirect: 'error',
  signal: AbortSignal.timeout(600000),
  headers: {
    Authorization: `Bearer ${openAiKey}`,
    'Content-Type': 'application/json',
  },
  body: JSON.stringify(buildOpenAiReviewRequestPayload({
    model,
    systemPrompt,
    userPrompt,
    maxOutputTokens,
    reasoningEffort,
  })),
});

if (!openAiResponse.ok) {
  const text = await openAiResponse.text();
  console.error(buildOpenAiErrorDiagnostics({
    status: openAiResponse.status,
    statusText: openAiResponse.statusText,
    headers: openAiResponse.headers,
    bodyText: text,
  }));
  throw new Error(`OpenAI API ${openAiResponse.status} ${openAiResponse.statusText}. See OpenAI error diagnostics above.`);
}

const openAiPayload = await openAiResponse.json();
const extractedReviewBody = extractReviewBodyFromOpenAiPayload(openAiPayload);
const structurallyValidReviewBody = isStructurallyValidReviewBody(extractedReviewBody);
console.log(buildOpenAiResponseDiagnostics({
  payload: openAiPayload,
  extractedReviewBody,
  isStructurallyValid: structurallyValidReviewBody,
}));

if (openAiPayload.status !== 'completed' || !extractedReviewBody || !structurallyValidReviewBody) {
  throw new Error('OpenAI did not return a completed, valid review; no passing fallback is allowed.');
}
const reviewBody = extractedReviewBody;
await assertCurrentHead();
const taggedBody = `${codexReviewMarker}\n${reviewBody}`;

await githubRequest(`/repos/${owner}/${repo}/pulls/${pr.number}/reviews`, {
  method: 'POST',
  headers: {
    Accept: 'application/vnd.github+json',
    'Content-Type': 'application/json',
  },
  body: JSON.stringify({
    body: taggedBody,
    event: 'COMMENT',
    commit_id: pr.head.sha,
  }),
});

console.log('Codex review posted.');

if (/Severity:\s*(Critical|High)\b/i.test(reviewBody)) throw new Error('Codex found blocking Critical/High findings.');
