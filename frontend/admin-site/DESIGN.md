# Design System Specification: The Precision Console

## 1. Overview & Creative North Star
**Creative North Star: "The Orchestrated Intelligence"**

This design system moves away from the "generic enterprise dashboard" and toward a high-end diagnostic instrument. It treats data not as a chore to be managed, but as a critical narrative to be curated. We avoid the "cluttered grid" by employing intentional white space, sophisticated tonal layering, and an editorial approach to technical information.

The visual identity is defined by **Atmospheric Depth**. By utilizing tonal shifts instead of rigid lines, we create an interface that feels like a singular, cohesive piece of glass hardware. It is authoritative, surgical in its precision, and designed for the elite engineer who requires instant cognitive processing of complex SDK lifecycles.

---

## 2. Colors: Tonal Architecture
Our palette is rooted in stability. We use a hierarchy of blues to establish authority, while our semantic colors act as precise "beacons" within a neutral field.

### The "No-Line" Rule
**Explicit Instruction:** Do not use 1px solid borders to define sections or containers. Separation must be achieved through:
1.  **Background Shifts:** Placing a `surface_container_lowest` card atop a `surface_container_low` background.
2.  **Negative Space:** Using the spacing scale (`8`, `10`, or `12`) to create logical groupings.

### Surface Hierarchy & Nesting
Treat the UI as physical layers. Use the following tiers to define depth:
*   **Base Layer:** `background` (#f8f9fa) - The foundation.
*   **Secondary Work Area:** `surface_container_low` (#f3f4f5) - Large layout wrappers.
*   **Primary Interaction Surface:** `surface_container_lowest` (#ffffff) - High-priority cards and diagnostic modules.
*   **Interactive Elevation:** `surface_bright` (#f8f9fa) - For hover states and active selections.

### The "Glass & Gradient" Rule
To add "soul" to the console, primary CTAs and global headers should utilize a subtle linear gradient: 
`linear-gradient(135deg, primary (#005bbf) 0%, primary_container (#1a73e8) 100%)`.
For floating diagnostic overlays (e.g., trace logs), use **Glassmorphism**: `surface` color at 80% opacity with a `20px` backdrop-blur.

---

## 3. Typography: Technical Authority
We use **Inter** for its neutral, high-legibility character, paired with a strict monospace implementation for technical identifiers.

*   **Display & Headlines:** Used sparingly for system-level overviews. High-contrast sizing (e.g., `display-sm` next to `label-md`) creates an editorial, premium feel.
*   **The Monospace Rule:** Any data point that is a `trace_id`, `port`, `JSON`, or `version_number` must use a monospace font-family (Roboto Mono or JetBrains Mono) at `body-sm` or `label-md` sizing. This signals to the user that the data is "copy-pasteable" and technical.
*   **Hierarchy as Identity:** Use `title-lg` for card headers in `on_surface` (#191c1d), paired immediately with `label-sm` in `outline` (#727785) for metadata. This "Tight-Stack" typography is a signature of this system.

---

## 4. Elevation & Depth: Tonal Layering
Traditional shadows and borders create visual noise. We achieve focus through **Ambient Light Physics**.

*   **The Layering Principle:** A diagnostic card (`surface_container_lowest`) sits on a workbench (`surface_container_low`). No shadow is required; the 1-2% shift in hex value provides enough contrast for the human eye to perceive depth.
*   **Ambient Shadows:** If a component must "float" (e.g., a critical error modal), use a high-diffusion shadow:
    *   `box-shadow: 0 12px 40px rgba(25, 28, 29, 0.06);` (Using a tint of `on_surface`).
*   **The "Ghost Border" Fallback:** If accessibility requirements demand a container boundary, use a "Ghost Border": `outline_variant` (#c1c6d6) at **15% opacity**. Never use 100% opaque lines.

---

## 5. Components: The Diagnostic Toolkit

### Buttons & Actions
*   **Primary:** High-gloss gradient (Primary to Primary-Container). Roundedness: `md` (0.375rem).
*   **Secondary:** Ghost style. No background, `outline` text. On hover, transition to `surface_container_high`.
*   **Status Badges (Chips):** Forbid high-saturation backgrounds. Use **Subtle Fills**:
    *   *GA/Normal:* `on_surface` text with 10% opacity Green (#34a853) background.
    *   *Beta/Warning:* `on_surface` text with 10% opacity Amber (#fbbc04) background.

### Data Density Tables
*   **The Rule of Row-Space:** Forbid horizontal divider lines. Distinguish rows using a subtle `surface_container_low` background on `:hover`. 
*   **Cell Alignment:** Numeric data and technical IDs must be right-aligned and monospaced to allow for quick vertical scanning.

### Metrics Cards
*   **Anatomy:** Large `headline-lg` value in `primary`. Below it, a `label-sm` description.
*   **Visual Soul:** A micro-sparkline (10% opacity gradient) should sit in the background of the card to show trend without distracting from the "Now" value.

### JSON Viewers
*   **Container:** `surface_container_highest` (#e1e3e4). 
*   **Syntax:** Use the semantic palette. Keys in `primary`, Strings in `secondary`, Booleans in `tertiary`.

---

## 6. Do’s and Don’ts

### Do
*   **Do** use vertical white space (Spacing `6` or `8`) instead of horizontal lines to separate content modules.
*   **Do** nest containers. A `surface_container_highest` code block looks premium inside a `surface_container_lowest` card.
*   **Do** use "Optical Alignment." Icons in side-navigation should be slightly smaller than the text to ensure the text remains the primary focal point.

### Don’t
*   **Don't** use 1px #dadce0 borders for every card. It creates a "grid-prison" effect that slows down data processing.
*   **Don't** use standard "Drop Shadows." They feel dated and heavy. Use tonal shifts or ambient 4% opacity glows.
*   **Don't** mix sans-serif and monospace in the same string. Keep technical data strictly segregated into its own columns or blocks.
*   **Don't** use high-saturation backgrounds for status badges; they draw too much "foveal attention" away from the actual data values.