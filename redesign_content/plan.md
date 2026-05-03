# ReBirth RB-338 Website Improvement Plan

## Overview
Redesign the existing Astro-built ReBirth RB-338 archive website with:
1. **Hardware-style panels** — distinct rack-module visual sections with metallic borders, screws, and depth
2. **Enhanced RB-338 colors** — richer application of the amber/green/red/metallic palette
3. **Better navigation menu** — improved UX with active states, mobile hamburger, sticky behavior

## Current State Analysis
- Built with Astro v6.0.4, deployed at test.1ink.us/rb338
- Existing hardware aesthetic with dark metallic theme
- Colors: amber (#ffb000), green (#3dba66), red (#c41e3a), dark metallic (#1e1e1e, #262626)
- Components: Header, Hero Panel, Terminal, Sequencer, About, Featured Songs/Mods, Footer
- Navigation: simple row of buttons (Home, Songs, Mods, Docs)

## Stage 1: Design & CSS Architecture
- Create comprehensive design system with panel tokens
- Define panel types: chassis panels, module panels, LCD panels, grid panels
- Enhanced color variables with more depth (glows, shadows, gradients)
- Menu system: desktop horizontal + mobile hamburger

## Stage 2: Implementation
- **Subagent A: CSS System & Global Styles**
  - Rewrite rebirth-theme.css with panel system
  - Enhanced RB338 color application
  - Improved typography hierarchy
  - Animation system (LED pulses, glow effects)
  
- **Subagent B: HTML Structure & Components**
  - Reorganize page sections into distinct panels
  - Improve header/navigation markup
  - Add mobile menu structure
  - Better content organization within panels
  
- **Subagent C: Interactive Elements & Polish**
  - Mobile menu JS functionality
  - Scroll-based panel animations
  - Active nav state highlighting
  - Terminal typing effect

## Stage 3: Integration & Deploy
- Merge all improvements into single deployable HTML/CSS/JS bundle
- Deploy to static hosting
- Deliver to user
