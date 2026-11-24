# Signed GameCube updates

## Release identity

GitHub Releases owns the release ZIP and its provenance attestation. The
publisher extracts the DOL and return apploader from that ZIP, records their
lengths and SHA-256 digests, and signs the exact channel manifest with Ed25519.
GitHub Pages serves the signed envelope and immutable versioned assets together.
The console uses the pinned public key with the SDK’s libsodium verifier.
Mirrors transport public bytes. The signature covers the release revision,
commit time, channel, filenames, lengths, hashes and optional minimum build.
Only a different, strictly newer build on the selected channel is eligible.
The publisher enforces increasing semantic version and commit time per channel.
An unavailable mirror or a replayed old manifest can delay discovery.

## Startup and storage

Each launch checks once after network initialization and applies an available
update at Home. An idle Files or Library browser can start the same check.
When it finds a newer release, it closes its card readers and returns to Home
before installation. A signed trial starts at Home once even with Recent or
Autoload enabled. Confirmed builds resume the configured startup path.
The worker reads only the bounded signed manifest. GUI input,
card access, download and executable handoff stay on the foreground thread.
An outbound-only startup check can bring up the interface. Starting ordinary
network services follows the existing network setting, required adapter
initialization or an explicit network action. W5500 and ENC28J60 adapters
retain their ordinary startup services. A failed probe returns quietly. The transfer buffer is 32 KiB.
One whole aligned DOL is required to validate and hand off its exact bytes.
The startup check and the manual HTTP and SMB actions share the verification
and installation code.

Install the updater-capable DOL and return apploader once using the usual
SD download. Both executables route to slots on the Configuration Device,
so a root `ipl.dol`, `boot.dol`, a Depot `apps/indigo/default.dol` and the
return apploader all use the same installed update. The original executables
stay available as the recovery version. The DOL running after an update is
the signed release DOL, including the code used after an In-Game Reset.
Auxiliary SD package assets such as translations still come from the ZIP.

| Interruption | Next ordinary start |
| --- | --- |
| Download or readback of the inactive slot | Previous confirmed slot or base |
| Writing the signed trial record | Previous confirmed slot or base |
| After trial publication, before the new Home | Previous confirmed slot or base. The same trial is suppressed until manually retried. |
| While promoting the trial at Home | A valid signed confirmed record and matching image can start. Otherwise the other slot or base starts. |
| A confirmed image fails its hash or DOL checks | Try the other confirmed slot, then the base |

A slot contains one DOL and either a signed trial or confirmed manifest.
A complete readback must match the signed digest before control transfers.
The new program verifies its own full commit and build time before renaming
its trial record to confirmed. The cold launcher reauthenticates the record
and the actual bytes it will execute. Hold X + Y through launch to use the
base directly. Removing the dedicated updates directory clears the slots.
Independent media failure or filesystem corruption can require a card backup.

## Qualification

Run the host signature, selection, failure and parser tests, the pinned PPC
build, stack/size checks and the Dolphin routes. On a physical GameCube, use
a backed-up test card and record the initial and final revisions for startup,
a cold boot, Depot launch and In-Game Reset. Check an offline launch, a manual
SMB update, insufficient space, damaged data and a failed trial. Interrupt
five installs across the boundaries above on test media. Record adapter and
configuration-device coverage separately for BBA, ENC28J60, W5500, SD,
IDE-EXI, KunaiGC, FlippyDrive and GC Loader.
