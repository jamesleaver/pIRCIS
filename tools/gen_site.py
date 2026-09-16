#!/usr/bin/env python3
"""Build the pIRCIS website into docs/ from README.md, LEARN.md and programs/.

The site is GitHub Pages serving docs/ from main. Nothing on it is written
by hand except the front page's opening and the stylesheet: every section
comes from the readme or the guide, so the site cannot drift from them.
Run it after either changes:

    python3 -m venv ~/.cache/pircis/venv && ~/.cache/pircis/venv/bin/pip install markdown
    ~/.cache/pircis/venv/bin/python tools/gen_site.py

Pages: index (what it is), get (the board, the emulator, the app), use
(the pages of the device), learn (the whole guide), programs (every
bundled program, with a copy button that pairs with PASTE on the device).
"""
import os
import re
import shutil
import sys
from pathlib import Path

try:
    import markdown
except ImportError:
    sys.exit("needs the markdown package: python3 -m venv ~/.cache/pircis/venv && "
             "~/.cache/pircis/venv/bin/pip install markdown, then run with that python")

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
REPO = "https://github.com/jamesleaver/pIRCIS"
BLOB = REPO + "/blob/main/"
STORE = "https://apps.apple.com/app/id6809655892"
DOMAIN = "pircis.fisheggs.au"
SITE = "https://" + DOMAIN + "/"

NAV = [("index.html", "HOME", "HOME"), ("get.html", "GET IT", "GET IT"), ("use.html", "USING IT", "USING IT"),
       ("learn.html", "LEARN IRCIS", "LEARN"), ("programs.html", "PROGRAMS", "PROGS")]
SUBMIT = REPO + "/issues/new?template=program.yml"
GITHUB_ICON = ('<svg viewBox="0 0 16 16" width="22" height="22" aria-hidden="true"><path fill="currentColor" '
               'd="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49'
               '-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 '
               '1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59'
               '.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 '
               '1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 '
               '3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0 0 16 8c0-4.42'
               '-3.58-8-8-8z"/></svg>')


# --- markdown ---------------------------------------------------------------

def slugify(text, sep="-"):
    """GitHub's heading ids, so the readme's own anchors keep working."""
    text = re.sub(r"<[^>]+>", "", text).strip().lower()
    text = re.sub(r"[^\w\s-]", "", text)
    return re.sub(r"\s+", sep, text)


def render(md_text, toc_depth="2-3"):
    md = markdown.Markdown(extensions=["tables", "fenced_code", "toc", "md_in_html"],
                           extension_configs={"toc": {"slugify": slugify, "toc_depth": toc_depth}})
    html = md.convert(md_text)
    return html, md.toc_tokens


def split_sections(md_text):
    """The readme as a list of (level, title, body) at ## and ### headings,
    with the text before the first ## as level 0."""
    out = []
    level, title, buf = 0, "", []
    fence = False
    for line in md_text.splitlines():
        if line.startswith("```"):
            fence = not fence
        m = None if fence else re.match(r"^(##|###) (.*)$", line)
        if m:
            out.append((level, title, "\n".join(buf)))
            level, title, buf = len(m.group(1)) - 1, m.group(2).strip(), []
        else:
            buf.append(line)
    out.append((level, title, "\n".join(buf)))
    return out


# --- links and images -------------------------------------------------------

def fix_links(html, anchors, page):
    """Repository-relative links go to the site page that holds them, or to
    GitHub for files the site does not carry."""
    def href(m):
        url = m.group(1)
        if url.rstrip("/") + "/" == SITE or url.startswith(SITE):   # the site's own pages, relative
            rest = url[len(SITE):] if url.startswith(SITE) else ""
            return 'href="%s"' % (rest or "index.html")
        if url.startswith(("http://", "https://", "mailto:")):
            return m.group(0)
        if url.startswith("#"):
            target = anchors.get(url[1:])
            if target and target != page:
                return 'href="%s%s"' % (target, url)
            return m.group(0)
        path, _, frag = url.partition("#")
        frag = "#" + frag if frag else ""
        if path == "LEARN.md":
            return 'href="learn.html%s"' % frag
        if path == "README.md":
            target = anchors.get(frag[1:], "index.html") if frag else "index.html"
            return 'href="%s%s"' % (target, frag)
        if path.startswith("programs/"):
            # The folder itself, a folder in it, or one program.
            rest = path[len("programs/"):].strip("/")
            if not rest:
                return 'href="programs.html"'
            if "/" not in rest:
                return 'href="programs.html#%s"' % slugify(rest)
            return 'href="programs.html#%s"' % slugify(Path(path).stem)
        if path.startswith("shots/"):
            return m.group(0)
        if path in ("ios/PRIVACY.md", "PRIVACY.md"):
            return 'href="privacy.html"'
        if path in ("ios/SUPPORT.md", "SUPPORT.md"):
            return 'href="support.html"'
        return 'href="%s%s%s"' % (BLOB, path, frag)
    return re.sub(r'href="([^"]+)"', href, html)


def grey_blanks(html, every_untagged=False):
    """A program listing gets a frame with a copy button, and its '.' cells
    are drawn faint, as the device draws them. On the guide and the programs
    page every untagged code block is a program, spaces inside its quoted
    strings and all; elsewhere a listing is a block of printable non-space
    lines, which leaves the shell commands and console transcripts alone."""
    def block(m):
        tagged = 'class="' in m.group(1)
        body = m.group(2)
        lines = re.sub(r"<[^>]+>", "", body).split("\n")
        lines = [l for l in lines if l]
        program = (every_untagged and not tagged) or \
                  (lines and all(re.fullmatch(r"[!-~]+", l) for l in lines) and "." in body)
        if not program:
            return m.group(0)
        body = body.replace(".", '<span class="d">.</span>')
        return ('<div class="frame"><button class="copy" type="button" title="Copy the program">copy</button>'
                + m.group(1) + body + m.group(3) + '</div>')
    return re.sub(r'(<pre><code[^>]*>)(.*?)(</code></pre>)', block, html, flags=re.S)


def copy_shots():
    src = ROOT / "shots"
    dst = DOCS / "shots"
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)


# --- page chrome -----------------------------------------------------------

def page(title, body, active, sidebar=None, description=""):
    # Each tab carries a long and a short label; the stylesheet shows the
    # short one where the long one would not fit.
    tabs = "".join(
        '<a class="tab%s" href="%s"><span class="long">%s</span><span class="short">%s</span></a>'
        % (" on" if href == active else "", href, label, short)
        for href, label, short in NAV)
    side = ('<aside class="side"><nav aria-label="Contents">%s</nav></aside>' % sidebar) if sidebar else ""
    icon = ('<a class="totop" href="#" hidden aria-label="Back to the top" title="Back to the top">'
            '<svg viewBox="0 0 16 16" width="22" height="22" aria-hidden="true"><path fill="none" stroke="currentColor" '
            'stroke-width="2" stroke-linecap="round" stroke-linejoin="round" d="M8 14V3M3.5 7.5 8 3l4.5 4.5"/></svg></a>'
            '<a class="gh" href="%s" aria-label="pIRCIS on GitHub" title="pIRCIS on GitHub">%s</a>' % (REPO, GITHUB_ICON))
    return """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%(title)s</title>
<meta name="description" content="%(description)s">
<link rel="canonical" href="%(site)s%(page)s">
<meta property="og:type" content="website">
<meta property="og:site_name" content="pIRCIS">
<meta property="og:title" content="%(title)s">
<meta property="og:description" content="%(description)s">
<meta property="og:url" content="%(site)s%(page)s">
<meta property="og:image" content="%(site)sshots/board.jpg">
<meta property="og:image:alt" content="pIRCIS running on a 4 inch ESP32 touchscreen">
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:title" content="%(title)s">
<meta name="twitter:description" content="%(description)s">
<meta name="twitter:image" content="%(site)sshots/board.jpg">
<link rel="icon" href="favicon.svg" type="image/svg+xml">
<script type="application/ld+json">%(ldjson)s</script>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:ital,wght@0,400;0,500;0,600;1,400&family=IBM+Plex+Sans:ital,wght@0,400;0,600;1,400&display=swap">
<link rel="stylesheet" href="site.css">
</head>
<body>
<div class="top">
<header class="bar">
  <a class="brand" href="index.html"><span class="p">p</span>IRCIS</a>
  %(icon)s
</header>
<nav class="tabs" aria-label="Pages">%(tabs)s</nav>
</div>
<div class="wrap%(cls)s">
%(side)s
<main class="body" data-page="%(slug)s">
%(body)s
</main>
</div>
<footer class="foot">
  <p>pIRCIS is copyright &copy; 2026 James Leaver, MIT licensed. IRCIS is the work of
  <a href="https://github.com/batman-nair/IRCIS">Arjun Nair</a>.
  <a href="%(repo)s">Source on GitHub</a> &middot;
  <a href="%(store)s">The app on the App Store</a> &middot;
  <a href="privacy.html">Privacy</a> &middot;
  <a href="support.html">Support</a></p>
</footer>
<script src="site.js"></script>
</body>
</html>
""" % {"title": title, "description": description, "tabs": tabs, "side": side,
       "cls": " with-side" if sidebar else "", "body": body, "repo": REPO, "icon": icon,
       "slug": active.replace(".html", ""), "site": SITE,
       "page": "" if active == "index.html" else active, "ldjson": ldjson(active, title, description),
       "store": STORE, "blob": BLOB}


def ldjson(active, title, description):
    """Structured data: the site and the software on the front page, an
    article for the guide, a plain web page elsewhere."""
    import json
    url = SITE + ("" if active == "index.html" else active)
    if active == "index.html":
        data = [{"@context": "https://schema.org", "@type": "WebSite", "name": "pIRCIS", "url": SITE},
                {"@context": "https://schema.org", "@type": "SoftwareApplication", "name": "pIRCIS",
                 "description": description, "url": SITE, "applicationCategory": "DeveloperApplication",
                 "operatingSystem": "iOS, iPadOS, macOS, Windows, Linux, ESP32",
                 "offers": {"@type": "Offer", "price": "0", "priceCurrency": "USD"},
                 "author": {"@type": "Person", "name": "James Leaver"},
                 "sameAs": [STORE, REPO]}]
    elif active == "learn.html":
        data = {"@context": "https://schema.org", "@type": "TechArticle", "headline": "Learn IRCIS",
                "description": description, "url": url, "author": {"@type": "Person", "name": "James Leaver"},
                "about": "IRCIS, the two-dimensional esoteric programming language"}
    else:
        data = {"@context": "https://schema.org", "@type": "WebPage", "name": title, "description": description, "url": url}
    return json.dumps(data, separators=(",", ":"))


def toc_html(tokens, page_href=""):
    def items(toks):
        out = "<ul>"
        for t in toks:
            out += '<li><a href="%s#%s">%s</a>' % (page_href, t["id"], t["name"])
            if t["children"]:
                out += items(t["children"])
            out += "</li>"
        return out + "</ul>"
    return items(tokens)


# --- the pages ---------------------------------------------------------------

def build():
    readme = (ROOT / "README.md").read_text()
    learn = (ROOT / "LEARN.md").read_text()
    sections = split_sections(readme)

    # Which page each readme section lands on, by its ## title.
    get_titles = {"App Store", "Board", "Emulator", "Raspberry Pi"}
    # Not on the website: the repository's layout and the licence belong
    # with the source, not with setting it up, using it or learning IRCIS.
    skip_titles = {"What's inside", "Copyright"}
    pages = {"index.html": [], "get.html": [], "use.html": []}
    intro = ""
    current = "index.html"
    for level, title, body in sections:
        if level == 0:
            intro = body
            continue
        if level == 1:
            current = ("index.html" if title in ("Learn IRCIS", "Three ways to run it")
                       else "get.html" if title in get_titles
                       else None if title in skip_titles else "use.html")
        if current is None:
            continue
        pages[current].append((level, title, body))

    # Every heading's slug, so cross-page anchors resolve.
    anchors = {}
    for href, secs in pages.items():
        for level, title, body in secs:
            anchors[slugify(title)] = href
    for line in learn.splitlines():
        m = re.match(r"^(#{1,3}) (.*)$", line)
        if m:
            anchors[slugify(m.group(2))] = "learn.html"

    DOCS.mkdir(exist_ok=True)
    copy_shots()

    # --- index: the readme's opening, then the three ways in
    intro_md = re.sub(r"^# pIRCIS\s*\n", "", intro)
    # The readme speaks of itself; the site speaks of the source.
    intro_md = intro_md.replace("built from this repository", "built from the source")
    # The "New here?" section is folded into the opening on the site.
    intro_html, _ = render(intro_md)
    intro_html = fix_links(intro_html, anchors, "index.html")
    intro_html = re.sub(r"<p>(.*?)</p>", r'<h1 class="lede">\1</h1>', intro_html, count=1, flags=re.S)
    # The readme points at the website; the website need not point at itself.
    intro_html = re.sub(r'<p>The website, <strong><a href="index.html">.*?</p>\s*', "", intro_html, count=1, flags=re.S)
    assert "The website," not in intro_html, "the readme's website line changed shape"
    # The three ways are the readme's own list, drawn as cards; what follows
    # them on the readme (the pictures, the guide) follows them here.
    rest_md = ""
    for level, title, sec_body in pages["index.html"]:
        rest_md += "%s %s\n%s\n" % ("#" * (level + 1), title, sec_body)
    rest_html, _ = render(rest_md)
    rest_html = fix_links(rest_html, anchors, "index.html")
    rest_html = re.sub(r'(<h2 id="three-ways-to-run-it">.*?</h2>\s*<ul>)', r'<section class="ways">\1', rest_html, count=1, flags=re.S)
    rest_html = rest_html.replace("</ul>", "</ul></section>", 1)
    body = intro_html + rest_html
    version = re.search(r'PIRCIS_VERSION "([^"]+)"', (ROOT / "src" / "Version.h").read_text()).group(1)
    column = ('<p class="k">pIRCIS %s</p><ul>'
              '<li><a href="%s">The app on the App Store</a></li>'
              '<li><a href="%s/releases/latest">Firmware for the board</a></li>'
              '<li><a href="%s">Source on GitHub</a></li>'
              '<li><a href="learn.html">Learn IRCIS</a></li>'
              '<li><a href="programs.html">The programs</a></li>'
              '<li><a href="%s">Send in a program</a></li></ul>'
              % (version, STORE, REPO, REPO, SUBMIT))
    (DOCS / "index.html").write_text(page(
        "pIRCIS: IRCIS programs on a pocket touchscreen, a computer or a phone", grey_blanks(body), "index.html", sidebar=column,
        description="pIRCIS runs IRCIS, the two-dimensional esoteric programming language, on a cheap touchscreen, on your computer, and on your phone."))

    # --- get and use: readme sections, in order, with a contents sidebar
    for href, heading, desc in [
        ("get.html", "Get pIRCIS", "Get pIRCIS: the app for iPhone, iPad and Mac, the ESP32 board, or the emulator for macOS, Windows and Linux."),
        ("use.html", "Using pIRCIS", "Using pIRCIS: the RUN, OUT, EDIT, PROG and SYS pages, the programs that come with it, view tags, and writing your own IRCIS programs.")]:
        md_text = ""
        for level, title, sec_body in pages[href]:
            md_text += "%s %s\n%s\n" % ("#" * (level + 1), title, sec_body)
        html, toc = render(md_text)
        html = grey_blanks(fix_links(html, anchors, href))
        (DOCS / href).write_text(page(heading, '<h1 class="title">%s</h1>' % heading + html,
                                      href, sidebar=toc_html(toc), description=desc))

    # --- learn: the guide whole
    learn_md = re.sub(r"^# Learn IRCIS\s*\n", "", learn)
    html, toc = render(learn_md, toc_depth="2")      # the rail lists the sections, not their parts
    html = re.sub(r"<hr />\s*(?=<h2)", "", html)     # the guide's rules; the headings draw their own
    html = grey_blanks(fix_links(html, anchors, "learn.html"), every_untagged=True)
    (DOCS / "learn.html").write_text(page(
        "Learn IRCIS: the language from nothing to your own programs", '<h1 class="title">Learn IRCIS</h1>' + html, "learn.html",
        sidebar=toc_html(toc),
        description="A guide to IRCIS from nothing: the runner, the stack, loops, numbers, variables, splitting, and a worked example."))

    # --- programs: every bundled program, with its grid and a copy button
    progs = ROOT / "programs"
    folders = sorted(p for p in progs.iterdir() if p.is_dir())
    # Programs people have sent in live beside the bundled ones, not among
    # them: the device carries programs/, the website carries both.
    contributed = ROOT / "contributed"
    if contributed.is_dir() and any(contributed.glob("*.txt")):
        folders.append(contributed)
    body = ['<h1 class="title">The programs</h1>',
            '<p>Every program that comes with pIRCIS, in the folders PROG shows them in. '
            'Copy one and paste it in: Ctrl/Cmd-V on the emulator, PASTE FROM THE CLIPBOARD under '
            'PROG &gt; New program on the phone. A runner starts at the top left, heading east, '
            'unless the program says otherwise.</p>',
            '<p class="submit">Written one of your own? <a href="%s">Send it in</a> through GitHub, '
            'with a name and a line about what it does, and it can go on this page under '
            'Contributed.</p>' % SUBMIT]
    toc = []
    for folder in folders:
        fid = slugify(folder.name)
        label = folder.name.capitalize()
        toc.append({"id": fid, "name": label, "children": []})
        body.append('<h2 id="%s">%s</h2>' % (fid, label))
        for f in sorted(folder.glob("*.txt")):
            text = f.read_text().rstrip("\n")
            pid = slugify(f.stem)
            name = f.stem.replace("-", " ")
            esc = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
            body.append('<article class="prog" id="%s"><h3>%s <a class="src" href="%s">source</a></h3>'
                        '<pre><code>%s</code></pre></article>'
                        % (pid, name, BLOB + "programs/" + folder.name + "/" + f.name, esc))
    html = grey_blanks("\n".join(body), every_untagged=True)
    (DOCS / "programs.html").write_text(page(
        "IRCIS programs to copy and run", html, "programs.html", sidebar=toc_html(toc),
        description="Every IRCIS program bundled with pIRCIS, ready to copy and paste into the emulator or the app."))

    # --- the app's privacy policy and support page, from ios/
    for src, href, title, desc in [
        ("PRIVACY.md", "privacy.html", "pIRCIS privacy policy", "pIRCIS collects no data. The app's privacy policy."),
        ("SUPPORT.md", "support.html", "pIRCIS support", "How to get help with the pIRCIS app, and what to include.")]:
        md_text = re.sub(r"^# .*\n", "", (ROOT / "ios" / src).read_text(), count=1)
        html, _ = render(md_text)
        html = fix_links(html, anchors, href)
        heading = "Privacy" if src.startswith("PRIVACY") else "Support"
        (DOCS / href).write_text(page(title, '<h1 class="title">%s</h1>' % heading + html, href, description=desc))

    (DOCS / "sitemap.xml").write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
        + "".join("  <url><loc>%s%s</loc></url>\n" % (SITE, "" if p == "index.html" else p)
                  for p in ["index.html", "get.html", "use.html", "learn.html", "programs.html", "privacy.html", "support.html"])
        + "</urlset>\n")
    (DOCS / "robots.txt").write_text("User-agent: *\nAllow: /\nSitemap: %ssitemap.xml\n" % SITE)
    (DOCS / "CNAME").write_text(DOMAIN + "\n")
    (DOCS / ".nojekyll").write_text("")
    print("site built in", DOCS)


if __name__ == "__main__":
    build()
