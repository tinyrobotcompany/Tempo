import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class PrivateOutputTests(unittest.TestCase):
    def test_permissive_umask_existing_file_and_symlink(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder/'probe.c'
            source.write_text('#define _POSIX_C_SOURCE 200809L\n#include "private_output.h"\nint main(int argc,char **argv){(void)argc;umask(0);FILE *f=private_output(argv[1]);if(!f)return 2;fputs("fixture",f);return fclose(f);}\n')
            program = folder/'probe'
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'tests'),str(source),'-o',str(program)],check=True)
            artifact = folder/'artifact'
            subprocess.run([str(program),str(artifact)],check=True)
            self.assertEqual(stat.S_IMODE(artifact.stat().st_mode),0o600)
            artifact.chmod(0o666)
            subprocess.run([str(program),str(artifact)],check=True)
            self.assertEqual(stat.S_IMODE(artifact.stat().st_mode),0o600)
            link = folder/'link';link.symlink_to(artifact)
            self.assertEqual(subprocess.run([str(program),str(link)]).returncode,2)
            self.assertEqual(artifact.read_text(),'fixture')

if __name__ == '__main__': unittest.main()
