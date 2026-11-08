// Renders the site's static pages into a site directory (tools/web_site.sh runs it):
//   node site_pages.cjs <site directory>
// index.html becomes the English page at / and one page per other language at /<code>/ (texts from site_text.js), and
// windows-11.html becomes the guide at /windows-11/ and /<code>/windows-11/ (texts from guide_text.js). A search engine reads
// the text in the HTML, not what site.js swaps in, so every page carries its own language, title, description, canonical
// address and the same set of hreflang links, and sitemap.xml lists them all. Pages load their files by absolute path,
// because the same file sits at several depths. web/index.html stays a page that works as it is (the dev loop copies it).
"use strict";

const fs = require("fs");
const path = require("path");
const {kSiteTexts} = require("./site_text.js");
const {kGuideTexts} = require("./guide_text.js");

const kOrigin = "https://openbeach.app";
const kLanguageCodes = Object.keys(kSiteTexts); // en first: it is the page at /
const kGuideSlug = "windows-11/";

const pagePath = (code, slug = "") => (code === "en" ? "/" : `/${code}/`) + slug;

function escapeHtml(value) {
    return value.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

// Texts mark emphasis as **bold**, like site.js's setRichText.
function richHtml(value) {
    return value.split("**").map((part, i) => (i % 2 === 0 ? escapeHtml(part) : `<strong>${escapeHtml(part)}</strong>`)).join("");
}

// Every page lists all its language versions, itself included, and the English one as the default.
function hreflangLinks(slug) {
    const links = kLanguageCodes.map((code) => `<link rel="alternate" hreflang="${code}" href="${kOrigin}${pagePath(code, slug)}">`);
    links.push(`<link rel="alternate" hreflang="x-default" href="${kOrigin}${pagePath("en", slug)}">`);
    return links.join("\n");
}

function setHead(html, code, slug, title, description) {
    const url = kOrigin + pagePath(code, slug);
    html = html.replace(/<html lang="en">/, `<html lang="${code}"${code === "en" ? "" : " data-fixed-language"}>`);
    html = html.replace(/<title>[^<]*<\/title>/, `<title>${escapeHtml(title)}</title>`);
    html = html.replace(/(<meta name="description" content=")[^"]*/, `$1${escapeHtml(description)}`);
    html = html.replace(/(<meta property="og:title" content=")[^"]*/, `$1${escapeHtml(title)}`);
    html = html.replace(/(<meta property="og:description" content=")[^"]*/, `$1${escapeHtml(description)}`);
    html = html.replace(/(<meta property="og:url" content=")[^"]*/, `$1${url}`);
    // The preview image says its text in the page's language; its alt text is the page's title.
    html = html.replace(/(<meta property="og:image" content=")[^"]*/, `$1${kOrigin}/social/openbeach-featured-${code}.jpg`);
    html = html.replace(/(<meta property="og:image:alt" content=")[^"]*/, `$1${escapeHtml(title)}`);
    html = html.replace(/<link rel="canonical" href="[^"]*">/, `<link rel="canonical" href="${url}">\n${hreflangLinks(slug)}`);
    return html;
}

// The inner HTML of every data-t element is replaced by its text. (No such element holds another element of its own tag.)
function fillTexts(html, texts) {
    return html.replace(/(<(\w+)\b[^>]*\sdata-t="(\w+)"[^>]*>)([\s\S]*?)(<\/\2>)/g, (match, open, tag, key, inner, close) => {
        if (typeof texts[key] !== "string") throw new Error(`no text "${key}" for ${open}`);
        return open + richHtml(texts[key]) + close;
    });
}

// A file next to the page becomes an absolute path (the page is served at several depths); a link to the guide stays in
// the page's language.
function absolutePaths(html, code) {
    html = html.replace(/(\shref=")windows-11\//g, `$1${pagePath(code, kGuideSlug)}`);
    return html.replace(/(<(?:script|img|link|a)\b[^>]*\s(?:src|href)=")([^"#?/:][^":]*)"/g, (match, before, target) => {
        if (/^(?:https?|mailto):/.test(target)) return match;
        return `${before}/${target}"`;
    });
}

function mainPage(template, code) {
    const texts = kSiteTexts[code];
    let html = setHead(template, code, "", texts.title, texts.description);
    html = fillTexts(html, texts);
    // The JSON-LD block names the site in the page's language, like the rest of the head.
    html = html.replace(/("@type": "WebSite", )/, `$1"inLanguage": "${code}", `);
    return absolutePaths(html, code);
}

function guidePage(template, style, code) {
    const texts = kGuideTexts[code];
    const languageNames = {en: "English", fr: "Français", it: "Italiano", de: "Deutsch", es: "Español", pl: "Polski"};
    const links = kLanguageCodes.map((other) => `<a href="${pagePath(other, kGuideSlug)}" hreflang="${other}" lang="${other}">${languageNames[other]}</a>`);
    let html = template.replace("@STYLE@", () => style);
    html = setHead(html, code, kGuideSlug, texts.title, texts.description);
    html = html.replace(/@HOME@/g, pagePath(code)).replace("@LANGUAGES@", escapeHtml(texts.languages)).replace("@LANGLINKS@", links.join("\n    "));
    html = fillTexts(html, texts);
    return absolutePaths(html, code);
}

function sitemap() {
    const entries = [];
    for (const slug of ["", kGuideSlug]) {
        for (const code of kLanguageCodes) {
            const alternates = kLanguageCodes.map((other) =>
                `    <xhtml:link rel="alternate" hreflang="${other}" href="${kOrigin}${pagePath(other, slug)}"/>`);
            alternates.push(`    <xhtml:link rel="alternate" hreflang="x-default" href="${kOrigin}${pagePath("en", slug)}"/>`);
            entries.push(`  <url>\n    <loc>${kOrigin}${pagePath(code, slug)}</loc>\n${alternates.join("\n")}\n  </url>`);
        }
    }
    return `<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9" xmlns:xhtml="http://www.w3.org/1999/xhtml">\n${entries.join("\n")}\n</urlset>\n`;
}

function write(site, relative, content) {
    const file = path.join(site, relative);
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, content);
}

function main() {
    const site = process.argv[2];
    if (!site) throw new Error("usage: node site_pages.cjs <site directory>");
    const index = fs.readFileSync(path.join(__dirname, "index.html"), "utf8");
    const guide = fs.readFileSync(path.join(__dirname, "windows-11.html"), "utf8");
    const style = index.match(/<style>\s*([\s\S]*?)<\/style>/)[1];
    for (const code of kLanguageCodes) {
        const folder = code === "en" ? "" : code;
        write(site, path.join(folder, "index.html"), mainPage(index, code));
        write(site, path.join(folder, kGuideSlug, "index.html"), guidePage(guide, style, code));
    }
    write(site, "sitemap.xml", sitemap());
}

main();
