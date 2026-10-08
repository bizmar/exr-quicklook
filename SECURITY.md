# Security policy

EXR Quick Look decodes files as soon as Finder shows them: a thumbnail is made
without anyone opening the file. A crafted EXR reaching the decoder is the
main risk, so reports about it are very welcome.

## Reporting a vulnerability

Please **don't open a public issue** for a security bug. Use GitHub's private
reporting instead: **Security → Report a vulnerability** on this repository
([direct link](https://github.com/bizmar/exr-quicklook/security/advisories/new)).
Only the maintainer sees the report.

Helpful to include:
- the file, or how to make it (a script, or the bytes changed in a known file);
- what happens: crash, hang, memory use, wrong output;
- your macOS version and Mac (Apple silicon or Intel);
- the EXR Quick Look version (in the app's window).

## What counts

**Security bugs:** anything a crafted file can cause beyond showing a wrong
image: crashes, hangs, runaway memory or CPU, reading or writing outside a
buffer, or anything that gets out of the extensions' sandbox. Bugs in the
bundled OpenEXR library count too; they will also be passed on to the
[OpenEXR project](https://github.com/AcademySoftwareFoundation/openexr/security).

**Ordinary issues:** wrong colours, the wrong layer chosen, a file that fails
to show. Please [open an issue](https://github.com/bizmar/exr-quicklook/issues/new?template=test-report.yml)
for those.

## What to expect

This is a one-person hobby project. Reports are answered and fixed on a
best-effort basis, usually within days, not hours. Fixes ship in a new
release, and only the **latest release** is fixed. You'll be credited in the
release notes unless you'd rather not be.

Releases after 0.2.0 are built by CI, with an attestation tying each DMG to
this repository and a VirusTotal scan whose result is attested too; see the
README's install section for how to check both. The C++ and Swift sources are
also analysed by GitHub's CodeQL on every push (Security → Code scanning).

How the code defends itself (checked arithmetic, hard limits, a decode
deadline) and what a past review found are described in
[docs/phase1-status.md](docs/phase1-status.md#adversarial-review-2026-10-08).
