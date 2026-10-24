#!/usr/bin/env python3
# vim: set sts=4 ts=8 sw=4 tw=99 et:
"""Generates version_gen.h from plugin-metadata.json.

plugin-metadata.json is the one place the core's name, author, description and
version live; this turns it into the defines plugin.cpp returns -- the same
script and header the native plugin samples use, so the core's metadata is
kept the way a plugin's is.

A version string may contain:
  {{semver}}         SEMVER from the environment (the release CI sets it),
                     "Local" otherwise
  {{github-sha}}     GITHUB_SHA_SHORT from the environment, "Local" otherwise
  {{git-shorthash}}  the short hash of the working tree it is generated from,
                     or "0" outside a git checkout
"""

import argparse
import json
import os
import subprocess
import sys

FIELDS = [
    ('name', 'PLUGIN_NAME'),
    ('alias', 'PLUGIN_ALIAS'),
    ('display_name', 'PLUGIN_DISPLAY_NAME'),
    ('description', 'PLUGIN_DESCRIPTION'),
    ('author', 'PLUGIN_AUTHOR'),
    ('url', 'PLUGIN_URL'),
    ('log_tag', 'PLUGIN_LOGTAG'),
    ('license', 'PLUGIN_LICENSE'),
    ('version', 'PLUGIN_FULL_VERSION'),
]

# Written only when the metadata has them (the core's "s2m credits").
OPTIONAL_FIELDS = [
    ('website', 'PLUGIN_WEBSITE'),
    ('repository', 'PLUGIN_REPOSITORY'),
]


def c_string(value):
    return '"{}"'.format(value.replace('\\', '\\\\').replace('"', '\\"'))


def git_shorthash(path):
    try:
        out = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'],
                             cwd=path, capture_output=True, text=True, timeout=10)
        if out.returncode == 0:
            return out.stdout.strip() or '0'
    except Exception:
        pass
    return '0'


def env_or_local(name):
    value = os.environ.get(name, '')
    return value if value else 'Local'


def load(metadata_path):
    with open(metadata_path, 'r', encoding='utf-8') as fp:
        return json.load(fp)


def render(metadata, shorthash='0'):
    tokens = {
        '{{semver}}': env_or_local('SEMVER'),
        '{{github-sha}}': env_or_local('GITHUB_SHA_SHORT'),
        '{{git-shorthash}}': shorthash,
    }

    lines = [
        '/* Generated from plugin-metadata.json -- do not edit. */',
        '',
        '#ifndef _PLUGIN_METADATA_INFORMATION_H_',
        '#define _PLUGIN_METADATA_INFORMATION_H_',
        '',
    ]

    fields = FIELDS + [f for f in OPTIONAL_FIELDS if f[0] in metadata]
    width = max(len(define) for _, define in fields)

    for key, define in fields:
        value = str(metadata.get(key, ''))
        for token, replacement in tokens.items():
            value = value.replace(token, replacement)
        lines.append('#define {} {}'.format(define.ljust(width), c_string(value)))

    # "credits": [{"role": ..., "name": ...}] -> the initializer of a
    # { role, name } array: { "role", "name" }, ...
    credits = metadata.get('credits')
    if credits:
        lines += ['', '#define PLUGIN_CREDITS \\']
        for i, credit in enumerate(credits):
            tail = ', \\' if i + 1 < len(credits) else ''
            lines.append('    {{ {}, {} }}{}'.format(c_string(str(credit['role'])), c_string(str(credit['name'])), tail))

    lines += ['', '#endif /* _PLUGIN_METADATA_INFORMATION_H_ */', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', required=True, help='path to plugin-metadata.json')
    parser.add_argument('--output', required=True, help='path of the version_gen.h to write')
    args = parser.parse_args()

    metadata = load(args.metadata)
    text = render(metadata, git_shorthash(os.path.dirname(os.path.abspath(args.metadata))))

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)

    # Only rewrite on change, so an unchanged metadata file does not force a rebuild.
    if os.path.exists(args.output):
        with open(args.output, 'r', encoding='utf-8') as fp:
            if fp.read() == text:
                return 0

    with open(args.output, 'w', encoding='utf-8') as fp:
        fp.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
