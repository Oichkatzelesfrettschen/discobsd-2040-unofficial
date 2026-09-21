# Unsupported platform archive

This boundary preserves imported platform code whose instruction-set
architecture cannot be established from the source or its recorded history.
The maintained build has no selector, entry point, or compatibility claim for
these files.

The two retained PicoC Flying Fox files form an incomplete port with
unimplemented I/O and file operations. The relocation map records them as
`legacy-unsupported-platform`, separate from proven non-ARM sources. Restoring
the port requires primary evidence for the target, an explicit machine tuple,
a complete implementation, and warning-clean tests.
