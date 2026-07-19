# Project conventions

- `src/app/AppInfo.h` is **user-maintained** (version, copyright, About-dialog
  extras). Never regenerate, rewrite, or "clean up" this file; when the About
  dialog needs new data, add a constant here only if the user asks, and
  preserve their existing values verbatim.
