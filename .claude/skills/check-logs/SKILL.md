---
name: check-logs
description: Quick check of F4SE plugin logs for errors and recent speech
disable-model-invocation: true
---

# Quick Log Check

Quickly analyze the fallout4access plugin logs without spawning a full subagent.

## Log Locations

Log paths are machine-specific. Check the auto memory file `local-paths.md` for actual paths.
The logs are in the standard F4SE log directory (`<Documents>\My Games\Fallout4\F4SE\`):
- **Debug log**: `fallout4access.log`
- **Speech log**: `fallout4access_speech.log`

## Analysis Steps

1. **Read the debug log** (last 100 lines):
   - Look for ERROR or WARN level messages
   - Check initialization status (Prism backend, hooks)
   - Note any exceptions or failures

2. **Read the speech log** (last 50 lines):
   - Show recent announcements with timestamps
   - Identify any gaps or unexpected output

3. **Report summary**:
   ```
   ## Log Status

   **Debug Log**: [timestamp of last entry]
   - Errors: [count]
   - Warnings: [count]
   - [List any errors/warnings with context]

   **Speech Log**: [timestamp of last entry]
   - Recent announcements:
     [last 5-10 speech entries]

   **Issues Found**: [none / list issues]
   ```

## Quick Checks

- If logs don't exist: "No logs found. Has the game been launched with the plugin?"
- If logs are old (>1 hour): Note the timestamp, may be stale
- If speech log is empty but debug log shows init success: "Plugin loaded but no speech output yet"
