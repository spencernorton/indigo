"""Run the console's content transactions against a FAT directory/cluster model."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
GUI = HERE.parents[1] / 'cube/swiss/source/gui'


class ContentReplace(unittest.TestCase):
    def test_interrupted_installs(self):
        code = (GUI / 'update.c').read_text()
        result = code[code.index('typedef enum {'):code.index('} update_result_t;') + len('} update_result_t;')]
        transactions = code[code.index('enum { CONTENT_LIVE'):code.index('/* The games in the folder')]
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / 'result.inc').write_text(result)
            (temp / 'transactions.inc').write_text(transactions)
            test = temp / 'content-recovery'
            subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Werror',
                            '-I', str(temp), '-I', str(GUI), str(HERE / 'fixtures/content_recovery.c'),
                            str(GUI / 'sha256.c'), '-o', str(test)], check=True)
            subprocess.run([str(test)], check=True)
