# Bundled licences

## lwIP

lwip.txt preserves the BSD 3-clause licence notice of the lwIP stack bundled
with the pinned libogc2 SDK, normalized without trailing whitespace. Both the
legacy distribution and `buildtools/sd_package.sh` copy this tracked file so
host and CI package checks work without requiring `$DEVKITPRO` on the host.

Source: https://github.com/extremscorner/libogc2/blob/cb7745aa099e65c0a3295f482f691ad63ba7b730/lwip/COPYING

## Noto Sans

noto-sans-ofl.txt is the SIL Open Font License of Noto Sans, the font Menu
Language's Ukrainian letters are drawn from: the IPL font has no Cyrillic, so
buildtools/ui/generate_cyrillic_font.py draws Noto Sans's into a glyph sheet
(cube/swiss/source/gui/ui_font_cyrillic.inc) that the DOL carries. The SD-card
ZIP includes it as swiss/indigo/LICENSE-NotoSans.txt.

Source: https://github.com/google/fonts/blob/main/ofl/notosans/OFL.txt
