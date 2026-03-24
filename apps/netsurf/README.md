# NetSurf Libraries Implementation Plan

This directory contains the implementations and porting layers for the NetSurf browser libraries, adapted for Retro-OS.

## Libraries Status:

- [ ] LibParserUtils: Basic parsing utilities.
- [ ] LibWapcaplet: String interning for high performance.
- [ ] Hubbub: HTML5 compliant parsing engine.
- [ ] LibCSS: CSS parsing and selection engine.
- [ ] LibDOM: Implementation of the W3C DOM.
- [ ] LibNSFB: Framebuffer abstraction for Retro-OS GUI.
- [ ] LibCURL: Networking layer using Retro-OS TCP stack + mbedTLS.
- [ ] LibPNG / LibJPEG: Image decoding support.

## Phase 1: Foundation (Current)

- Creating directory structure.
- Implementing minimal Hubbub-compatible parser utilities.
