// Shared TypeScript interfaces for the UI component library

export type LedStatus = 'online' | 'pending' | 'offline' | 'flicker' | 'flicker-green';

export type PanelVariant =
  | 'default'
  | 'amber'
  | 'green'
  | 'red'
  | 'chassis'
  | 'rackmount'
  | 'rackmount-module'
  | 'rackmount-chassis';

export type ActionVariant = 'pattern' | 'mix';

export type TransportVariant = 'play' | 'rec' | 'stop' | 'default';

export interface ScrewCornersProps {
  /** Add a cross / Phillips head indentation */
  cross?: boolean;
}

export interface LedProps {
  /** Visual state of the LED */
  status?: LedStatus;
  /** Text label next to the LED */
  label: string;
  /** Accessible label (defaults to the visible label) */
  ariaLabel?: string;
  /** Hide from screen readers when decorative */
  ariaHidden?: boolean;
  /** Additional CSS class for the root element */
  class?: string;
}

export interface LcdProps {
  /** Small label above the value */
  label: string;
  /** Main numeric or text value */
  value: string | number;
  /** Optional unit suffix (e.g. "+", "BPM") */
  unit?: string;
  /** Smaller text size for long values */
  size?: 'default' | 'sm';
  /** Accessible label override */
  ariaLabel?: string;
  /** Tooltip title for the LCD value */
  title?: string;
  /** Additional CSS class for the root element */
  class?: string;
}

export interface KnobProps {
  /** Label beneath the knob */
  label: string;
  /** Rotation in degrees (-180 to 180) */
  rotation?: number;
}

export interface StepButtonProps {
  /** Whether this step is active/illuminated */
  active?: boolean;
  /** Whether this step marks a beat (every 4th) */
  beat?: boolean;
  /** Accessible label */
  ariaLabel: string;
  /** Tooltip title */
  title?: string;
  /** Optional inline style for dynamic coloring */
  style?: string;
}

export interface ActionButtonProps {
  /** Visual variant: red pattern button or green mix button */
  variant?: ActionVariant;
  /** href for link buttons; omit for button element */
  href?: string;
  /** target attribute for links */
  target?: string;
  /** rel attribute for links */
  rel?: string;
  /** Click handler for button element */
  onclick?: string;
  /** Accessible label */
  ariaLabel?: string;
  /** Button type */
  type?: 'button' | 'submit' | 'reset';
}

export interface TransportButtonProps {
  /** Visual variant */
  variant?: TransportVariant;
  /** Navigation href */
  href: string;
  /** Accessible label */
  ariaLabel: string;
  /** Tooltip title */
  title?: string;
}

export interface PanelHeaderProps {
  /** Panel title text */
  title: string;
  /** Optional LED status */
  ledStatus?: LedStatus;
  /** Optional LED label */
  ledLabel?: string;
  /** Additional CSS classes */
  class?: string;
}

export interface HardwarePanelProps {
  /** Semantic HTML tag */
  tag?: 'section' | 'div' | 'article';
  /** Element id attribute */
  id?: string;
  /** Accessible label for the panel */
  ariaLabel?: string;
  /** ID of element that labels this panel */
  ariaLabelledBy?: string;
  /** Panel title (renders in header) */
  title?: string;
  /** LED status for the header */
  ledStatus?: LedStatus;
  /** LED label for the header */
  ledLabel?: string;
  /** Visual accent variant */
  variant?: PanelVariant;
  /** Enable entrance animation */
  animate?: boolean;
  /** Add corner screws */
  screws?: boolean;
  /** Cross-style screws */
  screwCross?: boolean;
  /** Additional CSS classes for the root element */
  class?: string;
  /** Optional header CSS class override */
  headerClass?: string;
  /** Optional body CSS class */
  bodyClass?: string;
}
