# Home label settling fixtures

The two 240×24 grayscale images contain only the neutral Library face name,
cropped from `LABEL_BOX` in real CI captures. The first has 234 thresholded
text pixels; the settled version has 313. Both pass the label detector, but
their masks fail the unchanged word-identity comparison.

Source: evaluation `70c938a`, [CI run 41361093116](https://github.com/spencernorton/indigo/actions/runs/41361093116),
the NTSC/component/GC Loader smoke artifact's `01-home.png` and `06-right-5.png`.
Detection uses the existing 4:3 normalization and maximum RGB channel.
No clock, device names, game art or settings are included in the crops.

The regression replays repeated arriving pixels while the emulated clock is
frozen, followed by settled pixels with clock progress. Host observations alone
must not turn the arriving label into the reference for the five-face loop.
