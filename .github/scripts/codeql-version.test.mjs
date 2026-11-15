import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, readdirSync} from 'node:fs';

const workflowDirectory = new URL('../workflows/', import.meta.url);

test('CodeQL steps share an immutable version in each workflow', () => {
  let checked = 0;
  for (const file of readdirSync(workflowDirectory).filter(name => /\.ya?ml$/.test(name))) {
    const workflow = readFileSync(new URL(file, workflowDirectory), 'utf8');
    const references = [...workflow.matchAll(/uses:\s*github\/codeql-action\/[^@\s]+@([^\s#]+)/g)];
    if (!references.length) continue;
    checked++;
    for (const [, ref] of references) {
      assert.match(ref, /^[a-f0-9]{40}$/, `${file}: CodeQL must be commit-pinned`);
    }
    assert.equal(new Set(references.map(([, ref]) => ref)).size, 1,
      `${file}: update all CodeQL steps together; mixed versions cannot exchange configuration`);
  }
  assert.ok(checked > 0, 'Expected at least one workflow using CodeQL');
});
