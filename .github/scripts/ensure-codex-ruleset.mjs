#!/usr/bin/env node
import fs from 'node:fs';
const token=process.env.REPO_ADMIN_TOKEN;
const repo=process.env.GITHUB_REPOSITORY;
if(!token||!/^[-\w.]+\/[-\w.]+$/.test(repo||''))throw Error('Set REPO_ADMIN_TOKEN (repository Administration: write) and GITHUB_REPOSITORY.');
const name='Tempo required checks';
const checks=JSON.parse(fs.readFileSync('.github/required-checks.json','utf8'));
async function request(path,method='GET',body){
 const response=await fetch(`https://api.github.com/repos/${repo}${path}`,{method,signal:AbortSignal.timeout(30000),headers:{Authorization:`Bearer ${token}`,Accept:'application/vnd.github+json','Content-Type':'application/json'},body:body?JSON.stringify(body):undefined});
 if(!response.ok)throw Error(`Ruleset request failed: HTTP ${response.status}`);
 return response.json();
}
const existing=(await request('/rulesets?per_page=100')).find(r=>r.name===name);
const detail=existing?await request(`/rulesets/${existing.id}`):null;
// Preserve unrelated rules and existing restrictions; add no bypass actors.
const rules=(detail?.rules||[]).filter(r=>r.type!=='required_status_checks');
const previous=detail?.rules?.find(r=>r.type==='required_status_checks')?.parameters?.required_status_checks||[];
const contexts=new Map(previous.map(c=>[c.context,c]));for(const context of checks)if(!contexts.has(context))contexts.set(context,{context});
rules.push({type:'required_status_checks',parameters:{required_status_checks:[...contexts.values()],strict_required_status_checks_policy:true}});
for(const type of ['deletion','non_fast_forward'])if(!rules.some(r=>r.type===type))rules.push({type});
const payload={name,target:'branch',enforcement:'active',conditions:{ref_name:{include:['refs/heads/main'],exclude:[]}},bypass_actors:detail?.bypass_actors||[],rules};
await request(existing?`/rulesets/${existing.id}`:'/rulesets',existing?'PUT':'POST',payload);
console.log('Required checks configured for main.');
