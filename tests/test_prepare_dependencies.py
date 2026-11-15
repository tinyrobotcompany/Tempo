import hashlib, importlib.util, tempfile, unittest, zipfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('prepare',Path(__file__).resolve().parents[1]/'scripts/prepare-tests.py')
prepare=importlib.util.module_from_spec(spec);spec.loader.exec_module(prepare)
class DependencyTests(unittest.TestCase):
 def test_remote_and_local_archives_restore_identical_paths(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);archives=[]
   for name,member in [('remote','library/src/a.c'),('local','src/a.c')]:
    p=root/(name+'.zip')
    with zipfile.ZipFile(p,'w') as z:z.writestr(member,'verified source')
    archives.append(p)
   dep={'name':'library','archive':True,'sha256':prepare.digest(archives[0]),'local_sha256':prepare.digest(archives[1]),'archive_prefix':'library/'}
   for i,p in enumerate(archives):
    target=root/str(i);prepare.restore(dep,p,target)
    self.assertEqual((target/'src/a.c').read_text(),'verified source')
   archives[0].write_text('tampered')
   with self.assertRaises(ValueError):prepare.restore(dep,archives[0],root/'bad')
 def test_traversal_rejected_before_extraction(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);p=root/'unsafe.zip'
   with zipfile.ZipFile(p,'w') as z:z.writestr('library/../../outside','bad')
   dep={'name':'library','archive':True,'sha256':prepare.digest(p),'archive_prefix':'library/'}
   with self.assertRaises(ValueError):prepare.restore(dep,p,root/'out')
if __name__=='__main__':unittest.main()
