#!/usr/bin/env python3
"""Compare the production resolver with frozen independent IANA expectations."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


class TimezoneReference(unittest.TestCase):
    def test_generator_rejects_an_unpinned_reference_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            archive, output = work / "wrong.tar.gz", work / "reference.tsv"
            archive.write_bytes(b"not the pinned IANA distribution")
            result = subprocess.run([os.environ.get("PYTHON", "python3"),
                                     str(HERE / "generate_timezone_reference.py"),
                                     "--archive", str(archive), "--table-library", str(work / "unused.so"),
                                     "--out", str(output)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("pinned IANA 2026f", result.stderr)
            self.assertFalse(output.exists())

    def test_all_names_and_future_transition_boundaries(self):
        rows = [line for line in (HERE / "fixtures/timezones_2026f.reference.tsv").read_text().splitlines()
                if line and not line.startswith("#")]
        self.assertEqual(len({line.split()[0] for line in rows}), 446)
        self.assertGreater(len(rows), 1800)
        driver = r'''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ui_time_sync.h"
#include "tzdata.h"
static int list(const struct TZData *tree,const char *prefix) {
 for(int i=0;i<tree->count;i++) {
  const struct TZData *child=&tree->timezones[i];char name[64];
  int n=snprintf(name,sizeof(name),"%s%s",prefix,child->name);
  if(n<0||(size_t)n>=sizeof(name))return 1;
  if(child->count) {if(list(child,name))return 1;}
  else if(child->timezone)puts(name);
 }
 return 0;
}
int main(int argc,char **argv) {
 if(argc==2&&!strcmp(argv[1],"--names"))return list(&TZData,"");
 char name[64];long long second;int expected;unsigned count=0;
 while(scanf("%63s%lld%d",name,&second,&expected)==3) {
  int32_t actual=0;
  if(!UITimeSync_ZoneOffset(name,(int64_t)second,&actual)||actual!=expected) {
   fprintf(stderr,"%s at %lld: got %d, expected %d\n",name,second,(int)actual,expected);
   return 1;
  }
  count++;
 }
 printf("%u IANA reference vectors passed\n",count);return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="indigo-tz-reference-") as directory:
            work = Path(directory)
            source, binary = work / "reference.c", work / "reference"
            source.write_text(driver)
            gui = ROOT / "cube/swiss/source/gui"
            command = shlex.split(os.environ.get("CC", "cc")) + [
                "-std=c11", "-Wall", "-Wextra", "-Werror", "-I" + str(gui),
                "-I" + str(ROOT / "cube/swiss/include"), str(source),
                str(gui / "ui_time_sync.c"), str(HERE / "tzdata_host.c"), "-o", str(binary)]
            subprocess.run(command, check=True)
            actual_names = subprocess.check_output([str(binary), "--names"], text=True).splitlines()
            self.assertEqual(set(actual_names), {line.split()[0] for line in rows})
            result = subprocess.run([str(binary)], input="\n".join(rows) + "\n",
                                    text=True, capture_output=True, check=True)
            self.assertEqual(result.stdout.strip(), f"{len(rows)} IANA reference vectors passed")


if __name__ == "__main__":
    unittest.main()
