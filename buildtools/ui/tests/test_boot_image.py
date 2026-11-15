#!/usr/bin/env python3
"""A DOL or ELF started from a card holds every section its header names.

boot_dol reads the whole file, then sidestep copies the sections the header
points at, with no length to stop it. The real check runs here: swiss.c's
boot_image_valid, wiiload.c's validator it shares with uploads, and util.c's
endsWith. Every size short of a whole DOL or ELF is refused, each copy
allocated to its exact size so the sanitized lane catches a read past it;
SDLOADER.BIN and branch BINs still load raw.
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from test_cheats_gx_stream import extract_function
from wiiload_source import SOURCE as WIILOAD

ROOT = Path(__file__).resolve().parents[3]
SWISS = (ROOT / "cube/swiss/source/swiss.c").read_text()
UTIL = (ROOT / "cube/swiss/source/util.c").read_text()

PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef uint8_t u8;typedef uint32_t u32;typedef uint64_t XXH64_hash_t;
typedef struct {char name[1024];u32 size;} file_handle;
#define ELFMAG "\177ELF"
#define SELFMAG 4
#define PATCH_BIN 0
/* Branch BINs start with a b instruction; the real one also resolves it. */
static u32 branchResolve(void *data,int type,u32 at) {
    const u8 *p=(const u8 *)data+at*4u;(void)type;
    return (p[0]&0xfcu)==0x48u?1u:0u;
}
/* Swiss knows one DOL by name: it passes only with its own size and hash. */
static int valid_dol_xxh3(const file_handle *file,XXH64_hash_t hash) {
    if(strcasecmp(file->name,"sd:/apps/known.dol"))return -1;
    return file->size==320u&&hash==42u;
}
'''

DRIVER = r'''
static void be32(u8 *p,u32 n) {p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static void be16(u8 *p,u32 n) {p[0]=n>>8;p[1]=n;}
/* The first size bytes of image, in a buffer of exactly that size. */
static bool starts(const char *name,const u8 *image,size_t size,XXH64_hash_t hash) {
    file_handle file={{0},(u32)size};strcpy(file.name,name);
    u8 *copy=malloc(size?size:1);assert(copy);memcpy(copy,image,size);
    bool ok=boot_image_valid(&file,copy,hash);free(copy);return ok;
}
static bool checkFile(const char *path) {
    FILE *fp=fopen(path,"rb");if(!fp)return false;
    if(fseek(fp,0,SEEK_END)!=0){fclose(fp);return false;}
    long len=ftell(fp);if(len<=0||fseek(fp,0,SEEK_SET)!=0){fclose(fp);return false;}
    u8 *buf=malloc((size_t)len);assert(buf);
    if(fread(buf,1u,(size_t)len,fp)!=(size_t)len){free(buf);fclose(fp);return false;}
    fclose(fp);
    char logical[1024];snprintf(logical,sizeof logical,"sd:/%s",path);
    bool ok=starts(logical,buf,(size_t)len,0);
    free(buf);
    return ok;
}
int main(int argc, char **argv) {
    if(argc>1) {
        for(int i=1;i<argc;++i) {
            if(!checkFile(argv[i])) {
                fprintf(stderr,"boot_image_valid rejected %s\n",argv[i]);
                return 1;
            }
        }
        printf("boot images: verified %d real build artifact(s) PASS\n",argc-1);
        return 0;
    }
    u8 dol[320]={0};memset(dol+256,0x55,64);
    be32(dol,256);be32(dol+0x48,0x80003100);be32(dol+0x90,64);be32(dol+0xe0,0x80003100);
    assert(starts("sd:/apps/browser.dol",dol,sizeof dol,0));
    for(size_t size=0;size<sizeof dol;size++)assert(!starts("sd:/apps/browser.dol",dol,size,0));
    assert(starts("sd:/apps/known.dol",dol,sizeof dol,42));
    assert(!starts("sd:/apps/known.dol",dol,sizeof dol,41));

    u8 elf[320]={0};memcpy(elf,ELFMAG,4);elf[4]=1;elf[5]=2;
    be16(elf+16,2);be16(elf+18,20);be32(elf+24,0x80003100);
    be32(elf+28,52);be16(elf+42,32);be16(elf+44,1);
    be32(elf+52,1);be32(elf+56,256);be32(elf+64,0x80003100);be32(elf+68,64);be32(elf+72,64);
    assert(starts("sd:/apps/app.elf",elf,sizeof elf,0));
    for(size_t size=0;size<sizeof elf;size++)assert(!starts("sd:/apps/app.elf",elf,size,0));
    /* An ELF loads as an ELF whatever its name, so it is checked as one. */
    assert(starts("sd:/SDLOADER.BIN",elf,sizeof elf,0));
    assert(!starts("sd:/SDLOADER.BIN",elf,sizeof elf-1,0));

    u8 raw[64];memset(raw,0xa5,sizeof raw);
    assert(starts("sd:/SDLOADER.BIN",raw,sizeof raw,0));
    assert(starts("sd:/sdloader.bin",raw,4,0));
    assert(!starts("sd:/SDLOADER.BIN",raw,3,0));
    assert(!starts("sd:/apps/raw.bin",raw,sizeof raw,0)); /* neither a DOL nor a BIN */
    be32(raw,0x48000100);
    assert(starts("sd:/apps/branch.bin",raw,sizeof raw,0));
    assert(!starts("sd:/apps/branch.bin",raw,3,0));
    assert(!starts("sd:/apps/empty.dol",raw,0,0));

    /* Locked L1 cache BSS (0xe0000000..0xe0004000) is accepted; overflow is refused. */
    u8 l1dol[320];memcpy(l1dol,dol,sizeof dol);
    be32(l1dol+0xd8,0xe0000000u);be32(l1dol+0xdc,0x4000u);
    assert(starts("sd:/apps/locked-cache.dol",l1dol,sizeof l1dol,0));
    be32(l1dol+0xdc,0x4020u);
    assert(!starts("sd:/apps/locked-cache.dol",l1dol,sizeof l1dol,0));

    /* dolxz / Game Boy Interface (.dol and .dol+cli): final data section ends
     * with a 0x400-byte embedded CLI slot seeded with argv[0]\n and stored
     * without trailing NUL padding so .cli options can be concatenated. */
    u8 gbi[0x2840 + 0x1400]={0};
    memset(gbi+0x100,0x60,0x2740);
    memset(gbi+0x2840,0xaa,0x1000);
    be32(gbi+0x00,0x100u);be32(gbi+0x48,0x800b7100u);be32(gbi+0x90,0x2740u);
    be32(gbi+0x1c,0x2840u);be32(gbi+0x64,0x800b9840u);be32(gbi+0xac,0x1400u);
    be32(gbi+0xd8,0x800bac40u);be32(gbi+0xdc,0x7820u);be32(gbi+0xe0,0x800b712cu);
    memcpy(gbi+0x3840,"gbi.dol\n",8);
    assert(starts("sd:/apps/gbi/gbi.dol",gbi,0x3848u,0));
    const char *preset="--aspect=4:3\n--format=hd60\n";
    memcpy(gbi+0x3848,preset,strlen(preset)+1u);
    assert(starts("sd:/apps/gbi/gbi-ossc.dol+cli",gbi,0x3848u+strlen(preset)+1u,0));
    /* Truncated before or inside argv[0]\n, or carrying binary garbage in the CLI slot, is refused. */
    assert(!starts("sd:/apps/gbi/gbi.dol",gbi,0x3840u,0));
    assert(!starts("sd:/apps/gbi/gbi.dol",gbi,0x3847u,0));
    assert(!starts("sd:/apps/gbi/gbi.dol",gbi,0x37ffu,0));
    gbi[0x3849]=0xffu;
    assert(!starts("sd:/apps/gbi/gbi-ossc.dol+cli",gbi,0x384au,0));
    /* A text section is never treated as an omitted CLI tail. */
    u8 badText[0x1400]={0};
    be32(badText+0x00,0x100u);be32(badText+0x48,0x80003100u);be32(badText+0x90,0x1000u);
    be32(badText+0xe0,0x80003100u);
    memcpy(badText+0xd00,"app.dol\n",8);
    assert(!starts("sd:/apps/bad.dol",badText,0xd08u,0));

    puts("boot images: whole DOLs, GBI/.dol+cli, locked-cache BSS and ELFs start; cut-short ones refused PASS");
    return 0;
}
'''


def require_wiring():
    """boot_dol refuses a file before it names the game or loads anything."""
    body = extract_function(SWISS, "void boot_dol(file_handle* file, int argc, char *argv[])")
    check = body.index("if(!boot_image_valid(file, buffer, hash))")
    assert check < body.index("gameID_set(")
    assert check < min(body.index(f"{kind}toARAM(") for kind in ("DOL", "ELF", "BIN"))
    assert "valid_dol_xxh3(" not in body
    # boot_dol zero-pads 0x400 bytes past file->size so .dol+cli tail slots never read heap garbage.
    assert "(size_t)file->size + 0x400u" in body
    assert "memset((u8*)buffer + file->size, 0, 0x400u);" in body
    # The helper treats as raw exactly what boot_dol loads raw.
    helper = extract_function(SWISS, "static bool boot_image_valid(")
    for test in ('endsWith(file->name, "/SDLOADER.BIN")', "branchResolve(buffer, PATCH_BIN, 0)",
                 "memcmp(buffer, ELFMAG, SELFMAG)"):
        assert test in helper and test in body, test


def main():
    require_wiring()
    parts = [line for line in WIILOAD.splitlines()
             if line.startswith(("#define WIILOAD_MAX_IMAGE ", "#define WIILOAD_CLI_SLOT "))]
    parts += [extract_function(WIILOAD, marker) for marker in (
        "static u32 wiiload_be32(", "static u32 wiiload_be16(", "static bool wiiload_range(",
        "static bool wiiload_cli_tail_valid(", "bool wiiload_image_valid(")]
    parts += [extract_function(UTIL, "char *endsWith("), extract_function(SWISS, "static bool boot_image_valid(")]
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp)
        (path / "boot.c").write_text(PREFIX + "\n\n".join(parts) + "\n" + DRIVER)
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1"]
        if "--sanitize" in sys.argv:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            if sys.platform != "darwin":
                command += ["-no-pie"]
        files = []
        if "--check-files" in sys.argv:
            idx = sys.argv.index("--check-files")
            files = [str(Path(p).resolve()) for p in sys.argv[idx + 1:]]
            if not files:
                raise SystemExit("--check-files requires at least one path")
        subprocess.run(command + [str(path / "boot.c"), "-o", str(path / "boot")], check=True)
        subprocess.run([str(path / "boot"), *files], check=True, timeout=30)


if __name__ == "__main__":
    main()
