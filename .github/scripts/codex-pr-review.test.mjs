import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtempSync,writeFileSync,readFileSync,existsSync,rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
import {spawnSync} from 'node:child_process';
const root=resolve('.');
function run(scenario,key='test-placeholder',overrides={}){
 const temp=mkdtempSync(join(tmpdir(),'tempo-review-'));
 try{
 const pr={number:1,title:'ci: checks',body:'Review me',head:{sha:'a'.repeat(40),ref:'changeset-release/main'},base:{sha:'b'.repeat(40),ref:'main'},user:{login:'developer'}};
 writeFileSync(join(temp,'event.json'),JSON.stringify({pull_request:pr}));
 writeFileSync(join(temp,'mock.mjs'),`
 import fs from 'node:fs';
 const scenario=${JSON.stringify(scenario)};const pr=${JSON.stringify(pr)};let checks=0;
 globalThis.fetch=async(url,options={})=>{
  if(options.redirect!=='error')throw Error('Authenticated requests must reject redirects');
  if(url==='https://api.openai.com/v1/responses'){
   if(scenario==='api-error')return new Response('unavailable',{status:503});
   const result={summary:'Reviewed',findings:scenario==='blocking'?[{severity:'High',title:'Bug',evidence:'file:1',impact:'crash',fix:'validate'}]:[],testsVerification:['Tests inspected'],risksFollowups:['Device testing remains']};
   return Response.json({status:scenario==='incomplete'?'incomplete':'completed',output_text:scenario==='invalid'?'{}':JSON.stringify(result)});
  }
  if(options.method==='POST'){
   const body=JSON.parse(options.body);if(body.commit_id!==pr.head.sha)throw Error('Wrong reviewed commit');
   fs.writeFileSync(${JSON.stringify(join(temp,'posted.json'))},JSON.stringify(body));return Response.json({id:1});
  }
  if(options.headers.Accept==='application/vnd.github.v3.diff')return new Response(scenario==='large'?'x'.repeat(120001):'diff --git a/file.c b/file.c\\n+changed');
  checks++;return Response.json({...pr,head:{...pr.head,sha:scenario==='moved'&&checks>1?'c'.repeat(40):pr.head.sha}});
 };
 `);
 const result=spawnSync(process.execPath,['--import',join(temp,'mock.mjs'),'.github/scripts/codex-pr-review.mjs'],{cwd:root,encoding:'utf8',env:{...process.env,GITHUB_EVENT_PATH:join(temp,'event.json'),GITHUB_REPOSITORY:'tinyrobotcompany/Tempo',GITHUB_TOKEN:'test-placeholder',OPENAI_API_KEY:key,...overrides}});
 return {status:result.status,posted:existsSync(join(temp,'posted.json')),review:existsSync(join(temp,'posted.json'))?JSON.parse(readFileSync(join(temp,'posted.json'),'utf8')):null,output:result.stdout+result.stderr};
 }finally{rmSync(temp,{recursive:true,force:true});}
}
test('complete review posts against exact head even for release-like branch names',()=>{const r=run('clean');assert.equal(r.status,0,r.output);assert.ok(r.posted);});
for(const scenario of ['clean','blocking'])test(`${scenario} review visibly identifies Codex Code Review`,()=>{const r=run(scenario);assert.ok(r.review,r.output);assert.ok(r.review.body.startsWith(`<!-- codex-review:${'a'.repeat(40)} -->\n## Codex Code Review\n\n`));assert.equal(r.review.event,'COMMENT');});
for(const scenario of ['api-error','incomplete','invalid','large','moved'])test(`${scenario} fails closed without a success comment`,()=>{const r=run(scenario);assert.notEqual(r.status,0);assert.equal(r.posted,false);});
test('blocking findings are posted and fail the check',()=>{const r=run('blocking');assert.notEqual(r.status,0);assert.ok(r.posted);});
test('missing key fails',()=>assert.notEqual(run('clean','').status,0));

test('untrusted API origin fails before posting or sending credentials',()=>{const r=run('clean','test-placeholder',{GITHUB_API_URL:'https://example.invalid'});assert.notEqual(r.status,0);assert.equal(r.posted,false);assert.match(r.output,/Unsupported GitHub API origin/);});
