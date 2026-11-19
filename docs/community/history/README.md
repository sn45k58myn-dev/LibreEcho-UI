# Frozen source history

The adjacent bundle preserves only the new source commits above upstream `b14f9eb4dc8343769eca86240bc258c9130fcc6b`. Its published source head after the authorship cleanup is `7119e28fc7464c321375f5b5df9d95c0051d3929`. Verify with `git bundle verify tested-source.bundle`, then fetch `HEAD` locally.

The original upstream commit must be available in the clone. If a shallow clone omits it, fetch that exact upstream base before verifying. No private signing seed, device data or firmware image is included.
