"""Compatibility across the supported Home Assistant range (hacs.json: 2026.3+)."""

from __future__ import annotations

try:  # Home Assistant 2026.9+ validates schemas with probatio (same API).
    import probatio as vol
except ImportError:  # 2026.3-2026.8
    import voluptuous as vol  # type: ignore[no-redef]

__all__ = ["vol"]
