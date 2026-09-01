# Video demo scripts

Production material for the NebulaScope presentation video — see
[STORYBOARD.md](STORYBOARD.md) for the scene-by-scene plan, narration
beats and recording notes.

Each `sceneN-*.nsc` drives the app through one scene of the video via the
script engine (reproducible takes; re-record a scene by re-running it).
Scripts use paths relative to the presenter's image data folder and end
WITHOUT `quit`, leaving the app live for the marked manual moments. They
are production aids, not tests — the data they reference is not in the
repository.
