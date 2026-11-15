"""Exercise the real pre-commit hook in isolated repositories, without network or hardware."""
import os, shutil, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class CommitHook(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.repo=Path(self.temp.name);self.git('init','-b','main')
        self.git('config','user.name','Hook Test');self.git('config','user.email','hook@example.invalid')
        hook=self.repo/'.githooks/pre-commit';hook.parent.mkdir();shutil.copy2(ROOT/'.githooks/pre-commit',hook)
        hook.chmod(0o755);self.git('config','core.hooksPath','.githooks')
        (self.repo/'scripts').mkdir()
        (self.repo/'scripts/test-all.sh').write_text('#!/bin/bash\nset -eu\ncd "$(dirname "$0")/.."\ngrep -qx pass case.txt\necho ran >> "$HOOK_TEST_LOG"\n')
        (self.repo/'case.txt').write_text('pass\n');self.git('add','.')
    def git(self,*args):
        env=os.environ.copy();env['HOOK_TEST_LOG']=str(self.repo/'ran.log')
        return subprocess.run(['git','-C',str(self.repo),*args],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    def test_every_commit_runs_tests(self):
        for i in range(2):
            result=self.git('commit','--allow-empty','-m',f'test: {i}')
            self.assertEqual(result.returncode,0,result.stdout)
        self.assertEqual((self.repo/'ran.log').read_text(),'ran\nran\n')
    def test_unstaged_fix_cannot_hide_staged_failure(self):
        (self.repo/'case.txt').write_text('fail\n');self.git('add','case.txt')
        (self.repo/'case.txt').write_text('pass\n')
        self.assertNotEqual(self.git('commit','-m','test: staged failure').returncode,0)
        self.assertEqual((self.repo/'case.txt').read_text(),'pass\n')
    def test_commit_all_preserves_git_index_environment(self):
        self.assertEqual(self.git('commit','-m','test: initial').returncode,0)
        (self.repo/'note.txt').write_text('one');self.git('add','note.txt')
        self.assertEqual(self.git('commit','-m','test: track').returncode,0)
        (self.repo/'note.txt').write_text('two')
        result=self.git('commit','-a','-m','test: automatic index')
        self.assertEqual(result.returncode,0,result.stdout)
    def test_missing_runner_blocks_commit(self):
        self.git('rm','--cached','scripts/test-all.sh')
        self.assertNotEqual(self.git('commit','-m','test: missing runner').returncode,0)
if __name__=='__main__':unittest.main()
