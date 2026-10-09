# Tell Apple: macOS can't read modern EXR files

EXR Quick Look fixes Finder thumbnails and the spacebar preview. It **cannot**
fix the rest of macOS: Preview and every other app that opens images through
Apple's system decoder (ImageIO) still can't read these files. Only Apple can
change that, and Apple decides what to fix partly by how many people report
it. A Feedback report takes about five minutes.

## What macOS can't read

Checked on macOS 27.0.1 (26A434), with `sips`, which uses the same system
decoder as Preview and Finder:

| EXR compression | Introduced | macOS reads it? |
|---|---|---|
| ZIP, PIZ, PXR24, B44, RLE | 2002-2006 | yes |
| **DWAA, DWAB** | OpenEXR 2.2, **2014** | **no** |
| **HTJ2K** (High-Throughput JPEG 2000) | OpenEXR 3.4, 2025 | **no** |

DWAA and DWAB are the standard lossy compression for comp and render output in
VFX and animation: Nuke, Arnold, V-Ray, RenderMan, Houdini and others write
them, and many studio pipelines use them by default because the files are a
fraction of the size. Without EXR Quick Look, macOS shows these files as a
generic icon with no preview.

Try it yourself in Terminal, on any DWAA file:

```bash
sips -g pixelWidth -g pixelHeight your-file.exr
```

A file macOS can read prints its size; a DWAA, DWAB or HTJ2K file prints
`<nil>`.

## How to file the report

1. Open **Feedback Assistant**. It comes with macOS: search for it in
   Spotlight (Cmd-Space). You can also use
   [feedbackassistant.apple.com](https://feedbackassistant.apple.com). Sign in
   with your Apple Account.
2. Start a new report (the compose button) and choose **macOS**.
3. For the area, pick the one closest to images, such as **Preview** or
   **Quick Look**. Apple routes reports internally, so the closest match is
   fine.
4. Paste the title and description below. Edit them freely: a sentence about
   your own work (what you make, how many EXRs you handle, which apps write
   them) counts for more than anything we can write for you.
5. **Attach a sample file.** A report Apple can reproduce in seconds gets
   further. Use one of your own DWAA renders, or this small synthetic one:
   [dwaa-multilayer-acescg.exr](https://github.com/bizmar/exr-quicklook/raw/main/Tests/Fixtures/corpus/dwaa-multilayer-acescg.exr)
   (48 KB, BSD-licensed test image).
6. Submit. You get an FB number. Apple rarely replies, but every report is
   counted.

### Title

```
ImageIO cannot decode OpenEXR files using DWAA, DWAB or HTJ2K compression
```

### Description

```
macOS's built-in EXR support (ImageIO) cannot read OpenEXR files that use
DWAA or DWAB compression (part of OpenEXR since version 2.2, 2014) or HTJ2K
(OpenEXR 3.4, 2025). Such files show a generic icon in Finder, have no Quick
Look preview, and apps that rely on ImageIO cannot display them.

DWAA/DWAB are the standard lossy compression for render and comp output in
VFX and animation: Nuke, Arnold, V-Ray, RenderMan and Houdini write them, and
many studio pipelines use them by default. The OpenEXR reference library
(Academy Software Foundation, BSD-licensed) has decoded them for over ten
years.

Steps to reproduce:
1. Take the attached DWAA-compressed .exr file.
2. Select it in Finder: generic icon, no thumbnail. (If a third-party Quick
   Look extension for EXR is installed, switch it off first.)
3. Press Space: no preview.
4. In Terminal: sips -g pixelWidth file.exr  ->  pixelWidth: <nil>
   The same image saved with ZIP compression reads correctly.

Expected: macOS reads all compression types in the current OpenEXR
specification, as the reference implementation does.

[Optional: a sentence about your own work and how often you hit this.]
```

## Why not just use EXR Quick Look?

Please do. But a Quick Look extension can only add thumbnails and previews.
Apple offers no way for third parties to add an image format to Preview or
the system decoder, so those stay broken until Apple updates it.
The two together cover Finder today and, if enough people ask, the whole
system later.
