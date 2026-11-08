# Bundled licences

## lwIP

lwip.txt preserves the licence header from libogc2 f0a7627's lwip/core/udp.c,
part of the SDK pinned by Indigo. The legacy distribution and SD-card ZIP
include it. This SDK predates the separate gamecube/share/licenses/lwip/COPYING
file expected by newer Swiss, so packaging reads this vendored copy instead.

Source: https://github.com/extremscorner/libogc2/blob/f0a7627/lwip/core/udp.c

## Noto Sans

noto-sans-ofl.txt is the SIL Open Font License of Noto Sans, the font Menu
Language's Ukrainian letters are drawn from: the IPL font has no Cyrillic, so
buildtools/ui/generate_cyrillic_font.py draws Noto Sans's into a glyph sheet
(cube/swiss/source/gui/ui_font_cyrillic.inc) that the DOL carries. The SD-card
ZIP includes it as swiss/indigo/LICENSE-NotoSans.txt.

Source: https://github.com/google/fonts/blob/main/ofl/notosans/OFL.txt
