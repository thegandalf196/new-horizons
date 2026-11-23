"""Plain-text release notes for the launcher's What's new window.

Release bodies come from the network and are treated strictly as text: Markdown
is flattened, links lose their targets and every note is size-capped. Offline,
the notes of the installed version come from the changelog bundled with it.
"""
# SPDX-License-Identifier: GPL-2.0-or-later

import html
from pathlib import Path
import re
import stat

from PortableReleaseUpdate import release_version, version_key, version_label

BUNDLED_NAME = 'CHANGELOG-New-Horizons.md'
MAX_CHARS = 20000
MAX_INPUT_CHARS = 200000
MAX_BUNDLED_BYTES = 1024 ** 2
SHORTENED = '\n\n[Release notes shortened.]'
OFFLINE = 'Release notes are not available offline.'
MISSING = 'No release notes are available for this version.'

# Control, zero-width and bidirectional formatting characters never reach the window.
UNSAFE = re.compile(r'[\x00-\x08\x0b-\x1f\x7f-\x9f\u200b-\u200f\u202a-\u202e\u2060-\u2069\ufeff]')
COMMENT = re.compile(r'<!--.*?(?:-->|\Z)', re.S)
TAG = re.compile(r'</?[A-Za-z][^<>\n]*>')
FENCE = re.compile(r'^[ \t]*(?:```|~~~).*$', re.M)
REFERENCE = re.compile(r'^[ \t]*\[[^\]\n]+\]:[ \t]*\S.*$', re.M)
IMAGE = re.compile(r'!\[([^\[\]\n]*)\]\([^()\n]*\)')
LINK = re.compile(r'\[([^\[\]\n]*)\]\([^()\n]*\)')
REFERENCE_LINK = re.compile(r'\[([^\[\]\n]+)\]\[[^\[\]\n]*\]')
AUTOLINK = re.compile(r'<(?:https?|ftp|mailto):[^<>\s]*>', re.I)
BARE_URL = re.compile(r'\b(?:https?|ftp)://\S+|\bwww\.\S+', re.I)
HEADING = re.compile(r'^[ \t]{0,3}#{1,6}[ \t]+([^\n]*)$', re.M)
RULE = re.compile(r'^[ \t]*(?:(?:-[ \t]*){3,}|(?:\*[ \t]*){3,}|(?:_[ \t]*){3,}|={3,})$', re.M)
QUOTE = re.compile(r'^[ \t]*>[ \t]?', re.M)
BULLET = re.compile(r'^([ \t]*)[-*+][ \t]+', re.M)
# Every span excludes its own opening delimiter, so hostile text stays linear.
STRONG = re.compile(r'\*\*(?=\S)([^*\n]+?)(?<=\S)\*\*|__(?=\S)([^_\n]+?)(?<=\S)__')
EMPHASIS = re.compile(r'(?<![\w*])\*(?=\S)([^*\n]+?)(?<=\S)\*(?!\*)|(?<![\w_])_(?=\S)([^_\n]+?)(?<=\S)_(?![\w_])')
STRIKE = re.compile(r'~~(?=\S)([^~\n]+?)(?<=\S)~~')
CODE = re.compile(r'`+([^`\n]*)`+')
ESCAPE = re.compile(r'\\([\\`*_{}\[\]()#+\-.!>~|])')
ESCAPED = re.compile('[\ue000-\ue07f]')
BLANKS = re.compile(r'\n{3,}')
SPACES = re.compile(r'(?<=\S)[ ]{2,}')


def shorten(text, limit=MAX_CHARS):
    if len(text) <= limit:
        return text
    cut = text.rfind('\n', 0, limit)
    return text[:cut if cut > limit // 2 else limit].rstrip() + SHORTENED


def flatten(markdown, limit=MAX_CHARS):
    """Markdown or HTML-ish text as plain text, or None when there is none."""
    if not isinstance(markdown, str):
        return None
    text = markdown[:MAX_INPUT_CHARS].replace('\r\n', '\n').replace('\r', '\n').replace('\t', '    ')
    # Backslash escapes stay literal: hide them from the span rules, restore at the end.
    text = ESCAPED.sub('', UNSAFE.sub('', COMMENT.sub('', text)))
    text = ESCAPE.sub(lambda match: chr(0xe000 + ord(match.group(1))), text)
    text = TAG.sub('', FENCE.sub('', REFERENCE.sub('', text)))
    text = IMAGE.sub(r'\1', text)
    text = LINK.sub(r'\1', text)
    text = REFERENCE_LINK.sub(r'\1', text)
    text = BARE_URL.sub('', AUTOLINK.sub('', text))
    text = RULE.sub('', text)
    text = HEADING.sub(lambda match: match.group(1).rstrip(' \t#'), QUOTE.sub('', text))
    text = BULLET.sub(lambda match: match.group(1) + '\u2022 ', text)
    text = CODE.sub(r'\1', text)
    text = STRIKE.sub(r'\1', STRONG.sub(lambda match: match.group(1) or match.group(2), text))
    text = EMPHASIS.sub(lambda match: match.group(1) or match.group(2), text)
    text = ESCAPED.sub(lambda match: chr(ord(match.group()) - 0xe000), text)
    text = UNSAFE.sub('', html.unescape(text))
    text = BLANKS.sub('\n\n', '\n'.join(SPACES.sub(' ', line).rstrip() for line in text.split('\n'))).strip('\n')
    return shorten(text, limit) if text.strip() else None


def retained(releases, versions):
    """Plain notes of the published releases whose versions are wanted."""
    notes = {}
    for release in releases if isinstance(releases, list) else ():
        version = release_version(release)
        if version is not None and version in versions and version not in notes and release.get('draft') is False:
            text = flatten(release.get('body'))
            if text:
                notes[version] = text
    return notes


def bundled(package, version):
    """The installed version's section of the changelog shipped in its package."""
    if package is None or not version:
        return None
    try:
        path = Path(package) / BUNDLED_NAME
        status = path.lstat()
        if not stat.S_ISREG(status.st_mode) or status.st_size > MAX_BUNDLED_BYTES:
            return None
        with path.open('rb') as stream:
            text = stream.read(MAX_BUNDLED_BYTES + 1).decode('utf-8', errors='replace')
    except OSError:
        return None
    return changelog_section(text, version)


def changelog_section(text, version):
    # 0.94.0 must not take the heading of 0.94.0-beta.1, nor beta.1 that of beta.10.
    heading = re.compile(r'^##[ \t]+v?' + re.escape(version) + r'(?![0-9A-Za-z.-])[^\n]*$', re.M)
    found = heading.search(text)
    if found is None:
        return None
    following = re.compile(r'^##?[ \t]', re.M).search(text, found.end())
    return flatten(text[found.end():following.start() if following else len(text)])


def compose(track, installed, offer=None, fetched=None, offline=None):
    """The window text: newest first, each version clearly headed.

    fetched maps versions to notes from the release check, or is None when no
    check succeeded; offline holds the bundled notes of the installed version.
    """
    entries = {}
    if installed:
        entries[installed] = 'installed'
    if offer and offer != installed:
        entries[offer] = 'available update'
    sections = ['Beta track' if track == 'beta' else 'Stable track']
    if not installed:
        # A beta package starts with an empty stable track.
        sections.append('No beta is installed yet.' if track == 'beta' else 'No stable version is installed yet.')
    for version in sorted(entries, key=version_key, reverse=True):
        text = (fetched or {}).get(version) or (offline if version == installed else None)
        heading = f'Version {version_label(version)} ({entries[version]})'
        sections.append(heading + '\n' + '=' * len(heading) + '\n\n' + (text or (MISSING if fetched is not None else OFFLINE)))
    return '\n\n\n'.join(sections)
