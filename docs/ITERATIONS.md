# Iterations

This file maps visible iteration names to their purpose, so it stays easy to say:

- "Go back to iteration X"
- "Restart from the state before the Web UI experiment"

## Current Baseline

- `V0.1` - Imported development baseline, stored in `snapshots/V0.1/`
- `V1.0` - First public release, published in `releases/V1.0/`

## How To Use

- Add one line for each meaningful working iteration
- Use the same iteration ID in the sketch header and commit message
- If a state should remain directly accessible as a file, also create a snapshot under `snapshots/<iteration>/`

## Example Format

- `V0.2` - First working iteration after baseline V0.1
- `V0.3` - Web UI spacing adjusted
- `V0.4` - Web UI validation changed
- `V0.5` - Rolled back layout experiment, kept validation
