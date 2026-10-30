#ifndef BLANKBANNER_H
#define BLANKBANNER_H

/* The fallback banner's 96 x 32 RGB5A3 pixels; no disc-header or text data. */
enum { BLANK_BANNER_BYTES = 96 * 32 * 2 };
extern const unsigned char blankbanner[BLANK_BANNER_BYTES];

#endif
