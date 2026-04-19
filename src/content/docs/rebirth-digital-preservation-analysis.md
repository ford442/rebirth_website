---
title: "ReBirth RB-338 Digital Preservation Analysis"
version: "2.0.1"
description: "Exhaustive analysis of ReBirth RB-338 archives, executables, mods, utilities, hardware interfaces, and cultural legacy."
---

# ReBirth RB-338 Digital Preservation Analysis

## Introduction: The Dawn of the Virtual Studio Environment

The late 1990s witnessed a paradigm shift in electronic music production, moving creation from physical studios to the digital desktop. Propellerhead Software, founded in Stockholm in 1994 by Ernst Nathorst-Böös, Marcus Zetterquist, and Peter Jubel, helped lead this transformation.

After launching ReCycle in 1994, Propellerhead released ReBirth RB-338 in 1997 as a virtual emulation of the Roland TB-303 and TR-808, later adding TR-909 support in version 2.0. Its skeuomorphic interface mirrored the original hardware with step-sequencer buttons, rotary knobs, and brushed metal faceplates, making acid, techno, and hip-hop production accessible to a new generation of producers.

ReBirth also introduced the ReWire protocol in 1998, enabling direct audio and sync routing into Cubase and opening inter-application audio workflows royalty-free. Despite these contributions, Propellerhead ended ReBirth development and support in September 2005 to focus on Reason.

This analysis catalogs the current ReBirth ecosystem, including core executable archives, `.rbs` compositions, `.rbm` mods, utilities like ReMaker and ReCycle, hardware interfaces, and cultural preservation.

---

## 1. The Core Executable: Navigating Digital Decay and Modern Emulation

When Propellerhead discontinued ReBirth, they released the commercial CD-ROM ISO as freeware. The original ReBirth Museum later went offline, scattering the executable across mirrors, archives, and peer-to-peer networks.

### Reliable executable sources

| Source / Repository | Direct URL / Identifier | Notes |
|---------------------|-------------------------|-------|
| Internet Archive (Base ISO) | https://archive.org/details/rebirthrb338 | 220.4 MB Windows 98 PC installation ISO with era-accurate dependencies.
| Internet Archive (Modernized Installers) | https://archive.org/details/rebirthrb338forwin7810 | 1.6 GB collection with 32-bit/64-bit installers for Windows 7–11 and WinHelp32 patches.
| CjCity Repository | http://cjcity.ru/2/downloader.php?id=20 | Community-verified alternative legacy download.
| Wikipedia citation magnet | Citation #19 on Wikipedia | Peer-to-peer magnet link maintained by editors.
| GitHub No-CD patch | https://github.com/TheKikGen/REBIRTH-NOCD | Bypasses CD verification for modern Windows.
| Wayback Machine snapshot | https://web.archive.org/web/20050924081030/http://rebirthmuseum.com/ | Historical documentation and FAQs from the original ReBirth Museum.

### 64-bit and compatibility challenges

ReBirth is a 32-bit app built for Windows 95/98, with one major modern failure point: the deprecated WinHelp32 engine. The software attempts to load legacy help files during launch, which modern Windows removes entirely, preventing startup.

Common preservation strategies:

- Use the WinHelp32 patch or 64-bit installer bundles from archive.org.
- Install in a Windows 95/98 virtual machine using QEMU, VirtualBox, or VMware for the most authentic environment.
- Use Macintosh Classic Mode on PowerPC hardware or Mac OS 9 emulation for Mac users.

### Preservation warning for webmasters

Legacy downloads can become compromised over time. Prefer moderated, trusted archives and avoid pointing users at generic search-engine ZIPs or unverified torrents.

---

## 2. The Compositional Archive: Rescuing `.rbs`

ReBirth compositions are saved as `.rbs` files, which store the entire virtual studio state: sequencers, knob positions, mixer levels, automation curves, effects settings, and more. These files are not standard audio and require ReBirth to play back.

### Digital archaeology and the CCUMA legend

The most coveted archive is the so-called "Computer Controlled Underground Mod Archive" (CCUMA), a Hotline Community-era dataset of thousands of ReBirth compositions. Recoveries claim approximately 3,600–4,000 tracks in under 15 MB of compressed data, but veteran users believe legendary files are still missing.

### Modern `.rbs` hosting strategy

- GitHub: https://github.com/peffre/ReBirth-RB-338 is a primary staging ground for recovered `.rbs` files and offers a safer, malware-free source.
- Archive.org: bundled demo tracks and author songs appear in historical collections.
- Decentralized torrents: still useful for niche recoveries, though extremely slow due to low seed counts.

### Preservation advice

Websites can add significant value by verifying `.rbs` integrity, documenting compressor formats like StuffIt `.sit`, and re-hosting validated archives.

---

## 3. ReMaker and the Essential Modernization Workflow

ReMaker is the key utility for modernizing `.rbs` data. It parses ReBirth song files and exports MIDI sequences that retain note, slide, accent, and timing information.

### Why ReMaker matters

- Converts legacy `.rbs` compositions into usable MIDI for Ableton Live, FL Studio, Reason, and DAWs.
- Rescues musical structure even when the original ReBirth audio engine cannot be used.
- Is distinct from unrelated software with similar names, so host the correct executable and clearly label it.

### Common confusion to avoid

- AVS Video ReMaker is unrelated and should not be linked from a ReBirth archive.
- ReMaker should be described as the tool that extracts sequencer data from `.rbs` for modern production.

---

## 4. The RBM Modification Culture

ReBirth RB-338 encouraged graphical and sample-based modifications through `.rbm` files. These mods changed both audio samples and the GUI, creating a vibrant user-driven ecosystem.

### Why RBMs matter

- They allowed community members to redesign the interface and replace drum samples.
- Default mods demonstrated the power of user-created skins and sample swaps.
- Legendary mods like "Red Stripe" and "Infernalizer" became cultural touchstones.

### Safe RBM sources

- NordBeat ReBirth Corner: historically a key hub for `.rbm` files and nostalgia content.
- Peffre GitHub repo: categorizes legacy `.rbm` files and is a safe source of archived mods.
- Wayback Machine snapshots: useful for recovering .rbm files from vanished legacy sites.

---

## 5. Mod Development Utilities: ModPacker and ReNovator

A complete repository should also preserve the tools for creating mods.

### ModPacker

- Official utility shipped with ReBirth.
- Assembles `.WAV` samples and image files into `.rbm` mods.
- Requires strict filename conventions and no preview mechanism.

### ReNovator

- Third-party wrapper for ModPacker.
- Offers a graphical preview, semantic file naming, and drag-and-drop workflow.
- Converts modern assets into legacy-compatible formats for ReBirth.

Preserving both tools enables users to become creators rather than passive archivists.

---

## 6. Clarifying ReCycle vs. Reslice

Many modern users confuse Propellerhead's legacy ReCycle with unrelated iOS software called ReSlice.

### Key distinction

- **ReCycle** (1994) is Propellerhead's loop-slicing tool that produced REX/REX2 files.
- **ReSlice** is a VirSyn iOS app with no direct connection to ReBirth.

An archival site should explicitly explain this lineage and direct users to ReCycle when describing legacy loop-slicing workflows.

---

## 7. Tangible Hardware Interfaces and MIDI Control

ReBirth's MIDI implementation enabled extensive physical control. Users could map nearly every on-screen parameter to external MIDI hardware via standard CC messages.

### Hardware integration highlights

- ReBirth Quick Mapping made physical controllers easy to assign.
- Custom MIDI rigs became a major subculture for tactile ReBirth workflows.
- This MIDI-first architecture influenced modern DAW hardware mapping.

### Notable projects

- **Look Mum No Computer**: custom 64-knob MIDI controller built from vintage iMac shells.
- **AudioPilz**: recreated a complete analog ReBirth-style studio using TB-303, TR-808, TR-909, and sync hardware.

### Analog hybrid workflows

- Doepfer MS-404 was a popular analog companion to ReBirth, receiving MIDI from the software and outputting real analog audio.
- The MS-404's mod-friendly design mirrored the digital modding culture of ReBirth.

---

## 8. Modern Hardware Equivalents

A modern archive should also link to hardware that embodies ReBirth's original vision.

| Device | ReBirth Equivalent |
|--------|-------------------|
| Roland Boutique TB-03 / TR-08 / TR-09 | Direct hardware recreations of the ReBirth synth and drum machines.
| Novation Circuit | Portable groovebox with a sketchpad workflow similar to ReBirth.
| Roland MC-101 | ZEN-Core-based groovebox for modern mobile production.
| Elektron Analog Rytm / Analog Four | Deep analog sequencing and drum synthesis inspired by ReBirth's concepts.

---

## 9. Peripheral Memorabilia and Cultural Legacy

ReBirth's cultural impact extends beyond software: physical memorabilia is rare, and preservation often relies on fan-made apparel, conceptual art, and community storytelling.

### Memorabilia

- Official ReBirth merchandise from the 1997–2005 era is scarce.
- Fan-made apparel and Reason community stores are the most reliable current sources.

### Conceptual art and cultural identity

- Austrian artist Michael Kargl referenced ReBirth in digital identity work, showing that the software has transcended tool status and become cultural mythology.

---

## 10. Archiving the ReBirth-to-Reason Lineage

ReBirth's mod culture evolved into Reason's ReFill ecosystem. ReFills became the successor to `.rbm` mods as a way to package sounds and instruments.

### Preservation parallels

- Community archivists collect early free ReFills and discontinued libraries.
- The same ethical code applies: preserve only legitimately unavailable files and avoid takedowns for abandoned content.
- Documenting this evolution contextualizes ReBirth within Reason's broader legacy.

---

## Conclusion

Building a ReBirth RB-338 archive is digital archaeology.

A complete repository should preserve:
- verified executables and compatibility patches,
- `.rbs` song archives and ReMaker workflows,
- `.rbm` mods and mod creation tools,
- MIDI hardware interfaces and cultural narratives.

This preserves not just a piece of software, but the social and technical ecosystem that ReBirth created for future generations.
