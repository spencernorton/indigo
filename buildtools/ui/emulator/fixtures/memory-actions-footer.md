# Memory Cards Actions footer fixture

`memory-actions-footer.png` is the maximum RGB channel of the exact
`FOOTER_BOX` crop (30, 442, 610, 464) from the demonstration-data screenshot
`85-memory-cards-copy-to.png` in evaluation 70c938a's
[CI run 41361093116](https://github.com/spencernorton/indigo/actions/runs/41361093116).
The capture is still on Actions with Move focused, despite its planned Copy
confirmation name. Its following capture shows Move's question.

Original capture SHA-256:
`4d05b8e877959bb3842de1aeea31332f697aeddb6938530a792a3ab09db7dd35`.
Crop SHA-256:
`b06755c903cb516274efb5fe39fca15e1d7224d2854a0accbf2ebd392796c062`.
The crop contains only the generic Cancel and Select controller hints.

The regression combines this footer with the existing themed browser/details
fixtures and a delayed panel transition. It reproduces the old blind sequence
reaching Move's question, then requires the observer to reach Copy instead.
Other cases reject missed Actions input, another footer, or a panel that stays.
This model isolates a missing observation barrier; it does not claim a measured
one-second native-console delay or establish a production input defect.
