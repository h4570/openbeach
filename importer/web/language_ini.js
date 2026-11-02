// The import's and the start layer's INI edits (loaded by import_worker.js with importScripts and by the site's page;
// importer/web/node_check.cjs requires it).
// The game numbers its languages like BeachLife.ini's [Language] Language=<index>; the importer names them by these codes.
"use strict";

const kLanguages = ["en", "fr", "it", "de", "es", "pl"];

// The code of a browser language tag ("pl-PL": "pl"); the importer ignores one it does not know.
function browserLanguage(tag) {
    return (tag || "").slice(0, 2).toLowerCase();
}

// The code of the language `text` (an INI) selects; null when it has no valid [Language] Language.
function languageOfIni(text) {
    let inSection = false;
    for (const raw of text.split(/\r?\n/)) {
        const line = raw.trim();
        if (line.startsWith("[")) {
            inSection = line.toLowerCase() === "[language]";
        } else if (inSection) {
            const match = /^language\s*=\s*(\d+)\s*$/i.exec(line);
            if (match) return kLanguages[Number(match[1])] || null;
        }
    }
    return null;
}

// The game's settings loader (src/game/game/settings_ini.h, LoadMain) reads nothing from an INI without
// [Beach Life Options] Version=1 and starts with its defaults (English), so a Language written alone would be ignored.
function withVersion(lines) {
    const header = lines.findIndex((line) => line.trim().toLowerCase() === "[beach life options]");
    if (header < 0) {
        lines.unshift("[Beach Life Options]", "Version=1");
        return;
    }
    for (let i = header + 1; i < lines.length && !lines[i].trim().startsWith("["); i++)
        if (/^version\s*=/i.test(lines[i].trim())) return;
    lines.splice(header + 1, 0, "Version=1");
}

// `text` (an INI, possibly empty) with [Language] Language set to the index of `language`; other settings stay. The result
// always carries the version key the game requires.
function withLanguage(text, language) {
    const index = kLanguages.indexOf(language);
    if (index < 0) throw new Error("unknown language " + language);
    const entry = "Language=" + index;
    const lines = text.split(/\r?\n/);
    while (lines.length > 0 && lines[lines.length - 1] === "") lines.pop();
    let section = -1; // line of the [Language] header while inside it
    let done = false;
    for (let i = 0; i < lines.length && !done; i++) {
        const line = lines[i].trim();
        if (line.startsWith("[")) {
            if (section >= 0) {
                lines.splice(i, 0, entry); // the section ended without a Language key
                done = true;
            }
            section = line.toLowerCase() === "[language]" ? i : -1;
        } else if (section >= 0 && /^language\s*=/i.test(line)) {
            lines[i] = entry;
            done = true;
        }
    }
    if (!done) {
        if (section < 0) lines.push("[Language]");
        lines.push(entry);
    }
    withVersion(lines);
    return lines.join("\r\n") + "\r\n";
}

// `text` with [Video] ResWidth and ResHeight, unless it names a resolution already (the game saves the player's there).
function withDefaultResolution(text, width, height) {
    if (/^\s*reswidth\s*=/im.test(text)) return text;
    const lines = text.split(/\r?\n/);
    while (lines.length > 0 && lines[lines.length - 1] === "") lines.pop();
    const entries = ["ResWidth=" + width, "ResHeight=" + height];
    const header = lines.findIndex((line) => line.trim().toLowerCase() === "[video]");
    if (header >= 0) lines.splice(header + 1, 0, ...entries);
    else lines.push("[Video]", ...entries);
    withVersion(lines);
    return lines.join("\r\n") + "\r\n";
}

if (typeof module !== "undefined") module.exports = {kLanguages, browserLanguage, languageOfIni, withLanguage, withDefaultResolution};
