/**
 * DSH Session Analysis Script
 * 
 * Reads all DSH session files from ~/.dsh/sessions/, decompresses and parses them,
 * and produces a comprehensive analysis of events, errors, and patterns.
 */

import { readFile, readdir } from 'node:fs/promises';
import { join, basename, dirname } from 'node:path';
import { constants, zstdDecompress } from 'node:zlib';
import { promisify } from 'node:util';
import { createHash } from 'node:crypto';

const zstdDecompressAsync = promisify(zstdDecompress);

// --- Zstd frame scanning (replicated from DSH source) ---

const ZSTD_MAGIC = 0xFD2FB528;

function scanZstdFrames(buffer, maxFrames = Infinity) {
  const frames = [];
  let offset = 0;

  while (offset < buffer.length) {
    const start = offset;
    if (buffer.length - offset < 4) return { frames, tornStart: start };
    if (buffer.readUInt32LE(offset) !== ZSTD_MAGIC) {
      throw new Error(`corrupt Zstandard session log: invalid frame magic at byte ${offset}`);
    }
    offset += 4;

    if (offset === buffer.length) return { frames, tornStart: start };
    const descriptor = buffer.readUInt8(offset);
    offset += 1;
    if ((descriptor & 0x18) !== 0) {
      throw new Error(`corrupt Zstandard session log: reserved frame-header bit at byte ${offset - 1}`);
    }

    const contentSizeFlag = descriptor >>> 6;
    const singleSegment = (descriptor & 0x20) !== 0;
    const checksum = (descriptor & 0x04) !== 0;
    const dictionaryFlag = descriptor & 0x03;
    const dictionaryBytes = dictionaryFlag === 3 ? 4 : dictionaryFlag;
    const contentSizeBytes = contentSizeFlag === 0
      ? (singleSegment ? 1 : 0)
      : 1 << contentSizeFlag;
    const remainingHeaderBytes = (singleSegment ? 0 : 1) + dictionaryBytes + contentSizeBytes;
    if (buffer.length - offset < remainingHeaderBytes) return { frames, tornStart: start };
    offset += remainingHeaderBytes;

    for (;;) {
      if (buffer.length - offset < 3) return { frames, tornStart: start };
      const blockHeader = buffer.readUIntLE(offset, 3);
      offset += 3;
      const lastBlock = (blockHeader & 1) !== 0;
      const blockType = (blockHeader >>> 1) & 0x03;
      const blockSize = blockHeader >>> 3;
      if (blockType === 0x03) {
        throw new Error(`corrupt Zstandard session log: reserved block type at byte ${offset - 3}`);
      }
      const payloadBytes = blockType !== 0x01 ? blockSize : 1;
      if (buffer.length - offset < payloadBytes) return { frames, tornStart: start };
      offset += payloadBytes;
      if (lastBlock) break;
    }

    if (checksum) {
      if (buffer.length - offset < 4) return { frames, tornStart: start };
      offset += 4;
    }
    frames.push({ start, end: offset });
    if (frames.length === maxFrames) return { frames };
  }

  return { frames };
}

// --- Analysis helpers ---

async function decompressZstdFile(filePath) {
  const buffer = await readFile(filePath);
  const { frames, tornStart } = scanZstdFrames(buffer);
  
  if (frames.length === 0) {
    throw new Error('No complete zstd frames found');
  }

  const plaintexts = [];
  for (const frame of frames) {
    const plaintext = await zstdDecompressAsync(buffer.subarray(frame.start, frame.end));
    plaintexts.push(plaintext);
  }

  // Also try to decompress torn tail
  if (tornStart !== undefined) {
    try {
      const torn = await zstdDecompressAsync(buffer.subarray(tornStart));
      plaintexts.push(torn);
    } catch {
      // Torn frame may be incomplete
    }
  }

  return Buffer.concat(plaintexts).toString('utf8');
}

function parseSessionLog(text) {
  const lines = text.split('\n').filter(l => l.trim());
  if (lines.length === 0) throw new Error('Empty session log');

  const headerLine = JSON.parse(lines[0]);
  if (headerLine.type !== 'session') throw new Error('First line is not a session header');

  const events = [];
  for (let i = 1; i < lines.length; i++) {
    try {
      const parsed = JSON.parse(lines[i]);
      // Handle packed chunk rows (text-chunks, reasoning-chunks, tool-call-chunks)
      if (parsed.type === 'text-chunks' || parsed.type === 'reasoning-chunks' || parsed.type === 'tool-call-chunks') {
        // These are packed storage rows; try to decode them
        if (parsed.events && Array.isArray(parsed.events)) {
          for (const ev of parsed.events) {
            events.push(ev);
          }
        }
      } else if (parsed.type) {
        events.push(parsed);
      }
    } catch {
      // Skip unparsable lines (torn tail)
    }
  }

  return { header: headerLine, events };
}

function analyzeEvents(events) {
  const counts = {};
  const turns = new Map(); // turn number -> { start, end }
  const steps = new Map();  // turn.step -> { start, end }
  const toolCalls = [];
  const toolResults = [];
  const errors = [];
  const userMessages = [];
  const assistantMessages = [];
  let totalTokens = { input: 0, output: 0, total: 0 };
  let tokenCountRecords = 0;

  for (const ev of events) {
    const type = ev.type || 'unknown';
    counts[type] = (counts[type] || 0) + 1;

    if (type === 'turn/start') {
      turns.set(ev.data?.turn, { start: ev.time, startSeq: ev.seq, trigger: ev.data?.trigger });
    } else if (type === 'turn/end') {
      const t = turns.get(ev.data?.turn) || {};
      t.end = ev.time;
      t.endSeq = ev.seq;
      t.reason = ev.data?.reason;
      turns.set(ev.data?.turn, t);
      if (ev.data?.reason?.kind === 'error') {
        errors.push({
          type: 'turn_error',
          turn: ev.data.turn,
          time: ev.time,
          seq: ev.seq,
          reason: ev.data.reason,
        });
      }
    } else if (type === 'step/start') {
      const key = `${ev.data?.turn}.${ev.data?.step}`;
      steps.set(key, { start: ev.time, startSeq: ev.seq });
    } else if (type === 'step/end') {
      const key = `${ev.data?.turn}.${ev.data?.step}`;
      const s = steps.get(key) || {};
      s.end = ev.time;
      s.endSeq = ev.seq;
      steps.set(key, s);
    } else if (type === 'tool/call') {
      toolCalls.push(ev);
    } else if (type === 'tool/result') {
      toolResults.push(ev);
      if (ev.data?.error) {
        errors.push({
          type: 'tool_error',
          turn: ev.data.turn,
          step: ev.data.step,
          callId: ev.data.callId,
          name: ev.data.name,
          time: ev.time,
          seq: ev.seq,
          error: ev.data.error,
        });
      }
      // Check for sandbox denials in tool results
      if (ev.data?.message?.content) {
        const content = typeof ev.data.message.content === 'string' 
          ? ev.data.message.content 
          : JSON.stringify(ev.data.message.content);
        if (content.includes('sandbox') && (content.includes('denied') || content.includes('denial') || content.includes('access denied'))) {
          errors.push({
            type: 'sandbox_denial',
            turn: ev.data.turn,
            step: ev.data.step,
            time: ev.time,
            seq: ev.seq,
            tool: ev.data.name,
            snippet: content.slice(0, 200),
          });
        }
        if (content.includes('timeout') || content.includes('timed out')) {
          errors.push({
            type: 'timeout',
            turn: ev.data.turn,
            step: ev.data.step,
            time: ev.time,
            seq: ev.seq,
            tool: ev.data.name,
            snippet: content.slice(0, 200),
          });
        }
      }
    } else if (type === 'user/message') {
      userMessages.push(ev);
    } else if (type === 'assistant/message') {
      assistantMessages.push(ev);
      if (ev.data?.usage) {
        totalTokens.input += ev.data.usage.input || ev.data.usage.inputTokens || 0;
        totalTokens.output += ev.data.usage.output || ev.data.usage.outputTokens || 0;
        totalTokens.total += ev.data.usage.total || ev.data.usage.totalTokens || 0;
        tokenCountRecords++;
      }
    }
  }

  return {
    counts,
    turns,
    steps,
    toolCalls,
    toolResults,
    errors,
    userMessages,
    assistantMessages,
    totalTokens,
    tokenCountRecords,
  };
}

function categorizeErrors(errors) {
  const categories = {
    turnErrors: errors.filter(e => e.type === 'turn_error'),
    toolErrors: errors.filter(e => e.type === 'tool_error'),
    sandboxDenials: errors.filter(e => e.type === 'sandbox_denial'),
    timeouts: errors.filter(e => e.type === 'timeout'),
  };

  // Analyze error patterns
  const toolErrorNames = {};
  for (const e of categories.toolErrors) {
    const key = e.error?.name || e.error?.code || 'unknown';
    toolErrorNames[key] = (toolErrorNames[key] || 0) + 1;
  }

  const turnErrorKinds = {};
  for (const e of categories.turnErrors) {
    const kind = e.reason?.kind || 'unknown';
    turnErrorKinds[kind] = (turnErrorKinds[kind] || 0) + 1;
    if (e.reason?.code) {
      turnErrorKinds[`error:${e.reason.code}`] = (turnErrorKinds[`error:${e.reason.code}`] || 0) + 1;
    }
  }

  return { ...categories, toolErrorNames, turnErrorKinds };
}

async function analyzeSession(filePath) {
  const dirName = basename(dirname(filePath));
  const parentDir = basename(dirname(dirname(filePath)));
  const workspace = parentDir.replace(/^--/, '').replace(/--$/, '').replace(/~002F/g, '/');
  
  const fileStats = await readFile(filePath).then(b => b.length).catch(() => 0);
  
  let text;
  try {
    text = await decompressZstdFile(filePath);
  } catch (err) {
    return {
      filePath,
      dirName,
      workspace,
      fileSize: fileStats,
      error: `Decompression failed: ${err.message}`,
    };
  }

  let header, events;
  try {
    const parsed = parseSessionLog(text);
    header = parsed.header;
    events = parsed.events;
  } catch (err) {
    return {
      filePath,
      dirName,
      workspace,
      fileSize: fileStats,
      error: `Parsing failed: ${err.message}`,
    };
  }

  const analysis = analyzeEvents(events);
  const errorCategories = categorizeErrors(analysis.errors);

  // Compute session duration
  const firstEventTime = events.length > 0 ? events[0].time : header.createdAt;
  const lastEventTime = events.length > 0 ? events[events.length - 1].time : header.createdAt;
  const durationMs = lastEventTime - firstEventTime;

  // Count tool calls by name
  const toolCallNames = {};
  for (const tc of analysis.toolCalls) {
    const name = tc.data?.name || 'unknown';
    toolCallNames[name] = (toolCallNames[name] || 0) + 1;
  }

  // Find unclosed turns
  const unclosedTurns = [];
  for (const [turnNum, t] of analysis.turns) {
    if (!t.end) {
      unclosedTurns.push({ turn: turnNum, startTime: t.start, trigger: t.trigger });
    }
  }

  return {
    filePath,
    dirName,
    workspace,
    fileSize: fileStats,
    header: {
      id: header.id,
      version: header.version,
      createdAt: header.createdAt,
      cwd: header.cwd,
      parentSession: header.parentSession,
      origin: header.origin,
      delegationDepth: header.delegationDepth,
    },
    stats: {
      totalEvents: events.length,
      eventTypes: analysis.counts,
      turns: {
        total: analysis.turns.size,
        unclosed: unclosedTurns,
      },
      steps: {
        total: analysis.steps.size,
      },
      toolCalls: {
        total: analysis.toolCalls.length,
        byName: toolCallNames,
      },
      toolResults: {
        total: analysis.toolResults.length,
      },
      userMessages: analysis.userMessages.length,
      assistantMessages: analysis.assistantMessages.length,
      tokenUsage: {
        ...analysis.totalTokens,
        recordCount: analysis.tokenCountRecords,
      },
      durationMs,
      firstEventTime,
      lastEventTime,
    },
    errors: {
      total: analysis.errors.length,
      ...errorCategories,
    },
    unclosedTurns,
  };
}

async function findSessionFiles(rootDir) {
  const results = [];
  async function walk(dir) {
    let entries;
    try {
      entries = await readdir(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      const fullPath = join(dir, entry.name);
      if (entry.isDirectory()) {
        await walk(fullPath);
      } else if (entry.name === 'session.jsonl.zstd') {
        results.push(fullPath);
      }
    }
  }
  await walk(rootDir);
  return results;
}

// --- Main ---

async function main() {
  const sessionsDir = join(process.env.HOME, '.dsh', 'sessions');
  console.error(`Scanning sessions in: ${sessionsDir}`);
  
  const files = await findSessionFiles(sessionsDir);
  console.error(`Found ${files.length} session files\n`);
  
  const results = [];
  for (const file of files) {
    console.error(`Analyzing: ${file}`);
    const result = await analyzeSession(file);
    results.push(result);
    
    // Print quick summary
    if (result.error) {
      console.error(`  ERROR: ${result.error}`);
    } else {
      const mins = Math.round(result.stats.durationMs / 60000);
      console.error(`  Events: ${result.stats.totalEvents} | Turns: ${result.stats.turns.total} | Errors: ${result.errors.total} | Duration: ${mins}m`);
    }
  }

  // Print full JSON report
  console.log(JSON.stringify(results, null, 2));
}

main().catch(err => {
  console.error('Fatal error:', err);
  process.exit(1);
});
