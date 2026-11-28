// Devices trust only ISRG Root X1/X2, so the deployed CA must be production
// Let's Encrypt and both the worker and the CA bootstrap job must receive it.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const read = (name) => readFileSync(new URL(`../../infra/certificates/${name}`, import.meta.url), 'utf8');

test('main.bicep defaults to production Let\'s Encrypt and still allows staging', () => {
  const main = read('main.bicep');
  assert.match(main, /@allowed\(\['staging', 'production'\]\)\nparam acmeEnvironment string = 'production'\n/);
  assert.match(main, /^ {4}environment: acmeEnvironment$/m);
});

test('worker and CA bootstrap job both take ACME_ENVIRONMENT from the parameter', () => {
  const compute = read('compute.bicep');
  const uses = compute.match(/\{ name: 'ACME_ENVIRONMENT', value: environment \}/g) ?? [];
  assert.equal(uses.length, 2);
  assert.doesNotMatch(compute, /ACME_ENVIRONMENT', value: '/);
});
