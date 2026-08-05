# DSH Session Analysis Report

**Date:** 2026-08-04  
**Sessions analyzed:** 8 (across 4 workspaces)  
**Total events:** 69,125  
**Total user turns:** 139  
**Total tokens:** 2,068,307 input / 1,406,243 output  

---

## Executive Summary

Analysis of 8 DSH sessions stored in `~/.dsh/sessions/` reveals **131 total errors** across 6 of 8 sessions. The most critical findings are:

1. **Error cascades after context window exhaustion** — once the 1M-token context limit is hit, the retry loop enters a death spiral of 503 "server busy" errors spanning dozens of turns and over an hour of wall-clock time, with no meaningful backoff or user notification.
2. **50% of sessions have unclosed turns** — 4 of 8 sessions were interrupted mid-turn, indicating sessions are frequently terminated without proper cleanup.
3. **59 command timeouts** across sessions, many caused by the agent using Linux-specific commands (`timeout`) on macOS.
4. **No proactive context-window monitoring** — 2 sessions hit the 1M token limit without any advance warning or automatic compaction trigger.

---

## 1. Session Inventory

| # | Session ID (short) | Workspace | Created | Events | Turns | Errors | Duration |
|---|-------------------|-----------|---------|--------|-------|--------|----------|
| 1 | ea44abe7 | dsh | 15:00 UTC | 55 | 1 | 0 | <1 min |
| 2 | 7314c13e | oss-sdcc-project | 15:33 UTC | 29,172 | 60 | 68 | 15.5 hr |
| 3 | 410df714 | oss-zephyr | 15:04 UTC | 13,466 | 21 | 24 | 11.1 hr |
| 4 | d7c2f069 | oss-zephyr | 15:21 UTC | 2,376 | 4 | 0 | 16 min |
| 5 | dfdaa07d | oss-zephyr | 15:02 UTC | 654 | 1 | 3 | 2 min |
| 6 | 74cf7ff4 | telos (subagent) | 07:01 UTC | 2,471 | 1 | 2 | 2 min |
| 7 | 4a2010cf | telos | 15:35 UTC | 11,452 | 45 | 33 | 15.5 hr |
| 8 | 743aae8d | telos | 20:57 UTC | 9,479 | 6 | 1 | 10.1 hr |

---

## 2. Bugs

### 2.1 Error Cascade After Context Window Exhaustion (CRITICAL)

**Severity:** High — wastes hours of compute and user waiting time.

**Pattern:** When a session exceeds the model's 1,048,576-token context window, the agent retries the same request repeatedly. The retry mechanism triggers 503 "Service is too busy" errors from the API, creating a cascade that can last 30+ turns and over an hour.

**Evidence from session `7314c13e`:**
- Turn 29: `CONTEXT_WINDOW_EXCEEDED` (requested 1,059,186 tokens vs 1,048,565 limit)
- Turns 34–59: **26 consecutive `SERVER` (503) errors** spanning 74 minutes
- Error run: `[CONTEXT_WINDOW_EXCEEDED, SERVER×26]`
- Many attempts fail at step 1 or 2 — the agent never recovers

**Evidence from session `410df714`:**
- Turn 15: `CONTEXT_WINDOW_EXCEEDED` (1,049,368 vs 1,048,576)
- Turns 16–22: 7 consecutive `SERVER` errors

**Root cause:** The retry logic (38 `llm/retry` events across 3 sessions) retries the same oversized request without trimming context or triggering compaction. The API's 503 response may be caused by the oversized payload, but the retry loop treats it as a transient failure.

**Recommended fix:**
- After a `CONTEXT_WINDOW_EXCEEDED` error, immediately trigger automatic compaction instead of retrying
- Implement exponential backoff with jitter for retries
- Surface a clear message to the user: "Context window full — compacting conversation history"
- Add a hard limit on consecutive same-code retries (e.g., max 3)

---

### 2.2 Unclosed Turns on Session Termination (HIGH)

**Severity:** Medium — data integrity concern, but likely harmless for resume.

**Finding:** 4 of 8 sessions (50%) contain `turn/start` events without matching `turn/end` events:

| Session | Unclosed Turn | Open Duration | Trigger |
|---------|--------------|---------------|---------|
| 7314c13e | Turn 60 | 129 min | user message |
| 74cf7ff4 | Turn 1 | 2 min | user message (subagent) |
| 4a2010cf | Turn 45 | 4 min | user message |
| 743aae8d | Turn 6 | 3 min | user message |

**Root cause:** Sessions terminated while a turn was in progress (process killed, Ctrl+C, crash, or shutdown). The `turn/end` with `interrupted` reason is only synthesized during crash recovery on reload — if the session was never reloaded, the turn remains open on disk.

**Impact:** The torn-tail repair logic (`JsonlTornMarker`) should handle this on next load. However, the prevalence suggests users frequently terminate sessions mid-turn.

**Recommended fix:**
- Add a SIGINT/SIGTERM handler that gracefully closes the current turn before exit
- Add a periodic health-check event to detect hung sessions
- Surface unclosed turns in `dsh session list` output

---

### 2.3 File System Tool Errors (MEDIUM)

**Severity:** Medium — causes wasted steps and user confusion.

**23 `FsError` tool errors across 4 sessions:**

| Error Code | Count | Explanation |
|-----------|-------|-------------|
| `FS_EDIT_NOT_FOUND` | 7 | Edit target string not found in file |
| `FS_STALE_VERSION` | 6 | File changed since last observation |
| `FS_NOT_OBSERVED` | 4 | File edited without first reading |
| `FS_SANDBOX_DENIED` | 1 | Access outside workspace |
| `FS_NOT_REGULAR_FILE` | 1 | Tried to edit a non-file |
| `FS_AMBIGUOUS_EDIT` | 1 | Edit string matched multiple locations |

**Pattern:** `FS_EDIT_NOT_FOUND` and `FS_NOT_OBSERVED` together suggest the agent is editing files based on stale mental models. `FS_STALE_VERSION` indicates race conditions between parallel tool calls (the agent reads a file, then another tool call modifies it before the edit lands).

**Recommended fix:**
- Add automatic file re-observation before retrying an `FS_EDIT_NOT_FOUND` error
- Rate-limit or sequence file edits to avoid `FS_STALE_VERSION` races
- Consider a "retry with refresh" hint in the error message

---

### 2.4 `GOAL_TOOL_AUTHORITY_REQUIRED` Error

**Severity:** Low — one occurrence.

Session `7314c13e` Turn 1 Step 169: A goal tool was called without proper authority. This is a single occurrence but suggests the agent may try goal operations without checking prerequisites.

---

## 3. UX Issues

### 3.1 Silent Error Cascades Waste User Time (CRITICAL)

**Finding:** In session `7314c13e`, 26 turns failed consecutively over 74 minutes with identical "Service busy" errors. The user had no visibility into this death spiral unless watching the terminal output.

**Impact:** The session consumed ~74 minutes of wall-clock time with zero productive work. The user may have been away from the keyboard, expecting work to complete.

**Recommended fix:**
- After N consecutive identical errors, pause and ask the user: "I've failed N times with the same error. Continue or try a different approach?"
- Surface error rate in the TUI status line
- Add a configurable `maxConsecutiveErrors` setting that auto-pauses the session

---

### 3.2 Sandbox Denials Create Confusing Workflows

**Finding:** 6 sandbox denials across 4 sessions. The agent tries to access files outside the workspace (e.g., `~/.zshrc`, `/usr/local/bin/sdcc.real`) and receives opaque denial messages.

**Example from session `dfdaa07d`:**
```
Error: [sandbox: file access denied under workspace-write mode]
[sandbox: escalation available]
```

The escalation mechanism exists but the UX flow is unclear. In session `dfdaa07d`, the agent hit 2 denials in the same turn (steps 3 and 5) without successfully escalating.

**Recommended fix:**
- Pre-flight check: warn the agent when it's about to call a tool on a path outside the sandbox
- Auto-suggest escalation with a clear "why this needs more access" explanation
- Add a `sandbox-permissions` preview that shows which paths would be accessible at each level

---

### 3.3 macOS `timeout` Command Not Found (MEDIUM)

**Finding:** Multiple sessions show `bash: line 1: timeout: command not found` errors. The `timeout` command is a GNU coreutil not present by default on macOS.

**Evidence:** Sessions `7314c13e` (Turn 1 Step 58, Turn 5 Step 48) and `4a2010cf` (Turn 13 Step 13, Turn 20 Step 2) all show the agent using `timeout` and getting command-not-found.

**Recommended fix:**
- The bash tool should detect missing `timeout` and use `perl -e 'alarm...'` or `gtimeout` (from coreutils) as fallback
- Add a runtime environment capability check that informs the agent about available commands
- Include `timeout` availability in the system prompt context

---

### 3.4 No Progress Visibility During Long Tool Runs

**Finding:** 59 timeouts across sessions, with many `bash` commands timing out at 60s or 120s. The agent receives only "[timed out after 60000ms]" with no partial output.

**Example:** Session `7314c13e` Turn 1 Step 81: a command timed out after 120s with "(no output)" — the user sees nothing for 2 minutes, then a timeout message.

**Recommended fix:**
- Stream partial stdout/stderr during long-running commands so the user sees progress
- Add estimated time remaining for known-slow operations
- Allow the user to extend a command's timeout mid-execution

---

### 3.5 Excessive Steps Per Turn Without User Feedback

**Finding:** Sessions average 20–36 steps per turn in complex sessions. Each step is one model call + tool executions. At 36 steps/turn, the agent is making 36 sequential LLM calls for a single user request — potentially taking 10+ minutes without visible intermediate results beyond the streaming text.

**Recommended fix:**
- Show "Step N/M" in the TUI status line during multi-step turns
- Collapse repetitive tool calls in the display (e.g., "Editing file X (attempt 3/5)")
- Add a configurable `maxStepsPerTurn` to prevent runaway loops

---

### 3.6 Title Generation Always Fires Twice

**Finding:** 7 of 8 sessions show exactly 2 `session/title` events but only 1 `session/title-llm-request` event. The subagent session has only 1 title event. This suggests the title is set, then immediately overwritten — potentially a race or redundant write.

---

## 4. Performance Problems

### 4.1 High Token Consumption Drives Context Window Exhaustion

**Finding:** The largest sessions consume enormous tokens:
- `7314c13e`: 655K input / 505K output tokens over 60 turns
- `4a2010cf`: 884K input / 372K output tokens over 45 turns
- `410df714`: 401K input / 493K output tokens over 21 turns

At ~20K input tokens per turn, the 1M context window fills after ~50 turns without compaction. Only 2 of 8 sessions have `compact/start` events, suggesting compaction is either not triggered aggressively enough or not available in all configurations.

**Recommended fix:**
- Trigger automatic compaction at 70% context window utilization (not 95%)
- Make compaction a default-on behavior for sessions exceeding N turns
- Add a token budget indicator to the TUI

---

### 4.2 Large File Artifacts from Long Sessions

**Finding:** Session file sizes range from 34KB to 8.2MB (compressed). The largest session (`7314c13e`) is 8.2MB compressed, which likely decompresses to ~50MB+ of JSON. Session loading time grows linearly with file size.

**Recommended fix:**
- Implement log rotation or segment files for very long sessions
- Add a `dsh session trim` command to archive old turns
- Consider more aggressive compaction that reduces stored event count

---

### 4.3 No Idle Detection for Abandoned Sessions

**Finding:** Two sessions lasted 15.5 hours (930 min), but the active work likely happened in much shorter bursts. The session clock includes idle time between user messages.

**Recommended fix:**
- Track active vs. idle time separately
- Auto-pause after N minutes of idle to free resources
- Show "last active" timestamp in session listings

---

## 5. Missing Features & Improvement Suggestions

### 5.1 Session Analytics Dashboard
No built-in way to view session statistics (tokens used, errors encountered, tool success rate). The data exists on disk but requires custom scripts to extract.

### 5.2 Intelligent Error Recovery
The agent retries identically after failures. A smarter approach would:
- Analyze the error code and adapt (e.g., re-read file before retrying edit)
- Escalate to the user after repeated failures with a summary of what went wrong
- Suggest alternative approaches when a tool repeatedly fails

### 5.3 Proactive Context Management
- Show a context-window utilization bar in the TUI
- Auto-suggest compaction when approaching the limit
- Warn when a tool output will push the context over the limit
- Truncate or summarize large tool outputs before they enter context

### 5.4 Cross-Platform Compatibility Checks
- Detect OS at session start and warn about unavailable commands
- Provide OS-appropriate alternatives in tool schemas
- Test common command patterns (e.g., `timeout`, `sed -i`) for platform compatibility

### 5.5 Session Health Monitoring
- Track consecutive error count and auto-pause
- Detect loops (same tool call with same arguments repeated N times)
- Surface health metrics in `dsh session list`

### 5.6 Better Subagent Visibility
The subagent session (`74cf7ff4`) has 2,471 events but only 1 turn — it's unclear from the parent session how much work the subagent did. A subagent progress indicator in the parent TUI would help.

### 5.7 Resume-Safety for Interrupted Sessions
50% of sessions have unclosed turns. The resume logic should:
- Auto-detect and close interrupted turns on next load
- Show a "recovering from interruption" message
- Allow the user to replay or discard the incomplete turn

---

## 6. Data Quality Notes

- The analysis script decompressed 8 zstd session files totaling ~20MB compressed
- 2 sessions (`d7c2f069` and `ea44abe7`) have zero tool calls — likely info-gathering or simple Q&A
- 1 session (`74cf7ff4`) is a subagent child of `743aae8d`
- 2 sessions (`7314c13e` and `410df714`) consumed >400K tokens each — candidates for compaction
- The `tool/result` count exceeds `tool/call` count in some sessions, which may indicate packed chunk rows being expanded correctly

---

## 7. Recommendations Summary

| Priority | Category | Issue | Suggested Action |
|----------|----------|-------|-----------------|
| P0 | Bug | Error cascade after context exhaustion | Trigger auto-compaction on `CONTEXT_WINDOW_EXCEEDED`; add retry backoff |
| P0 | UX | Silent multi-hour error cascades | Pause after N consecutive identical errors; surface in TUI |
| P1 | Bug | 50% sessions have unclosed turns | Add graceful shutdown handler; auto-repair on load |
| P1 | UX | macOS `timeout` command missing | Detect platform; provide fallback commands |
| P1 | Perf | High token consumption drives OOM | Proactive compaction at 70% context utilization |
| P2 | Bug | File tool errors from stale reads | Auto re-observe before retrying edit failures |
| P2 | UX | Sandbox denials create confusion | Pre-flight path check; clearer escalation UX |
| P2 | Feature | No session analytics | Add `dsh session stats` command |
| P3 | UX | No progress during long tool runs | Stream partial output; show elapsed time |
| P3 | Feature | No cross-platform checks | OS detection + command availability in system prompt |

---

*Report generated from 8 DSH sessions across 4 workspaces using custom analysis tooling against the JSONL/zstd persistence format.*
