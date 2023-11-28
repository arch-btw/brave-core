# Copyright (c) 2023 The Brave Authors. All rights reserved.
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# you can obtain one at https://mozilla.org/MPL/2.0/.

import logging
import re
import subprocess
import json

from functools import cached_property
from typing import List, Optional

import components.path_util as path_util

from components.perf_test_utils import GetProcessOutput


class BraveVersion:
  _is_tag: bool
  _git_hash: str
  _last_tag: str

  def __init__(self, version_str: str) -> None:
    m = re.match(r'v\d+\.\d+\.\d+', version_str)
    if m is not None:  # Brave tag (v1.62.35)
      revision = f'refs/tags/{version_str}'
      self._is_tag = True
    else:  # could be git hash
      revision = version_str
      self._is_tag = False

    git_hash = _GetGitHash(revision)
    if git_hash is None:
      _FetchRevision(revision)
      git_hash = _GetGitHash(revision)
      if git_hash is None:
        raise RuntimeError(f'Bad git revision {revision}')
    self._git_hash = git_hash

    if self.is_tag:
      self._last_tag = version_str
    else:
      package_json = json.loads(
          _GetFileAtRevision('package.json', self.git_hash))
      self._last_tag = 'v' + package_json['version']

    self.revision_number
    self.commit_date
    self.to_chromium_version

  @property
  def last_tag(self) -> str:
    return self._last_tag

  @property
  def is_tag(self) -> bool:
    return self._is_tag

  def to_string(self) -> str:
    if self._is_tag:
      return self._last_tag
    return f'{self._last_tag}+({self.git_hash})'

  @property
  def git_hash(self) -> str:
    return self._git_hash

  @cached_property
  def revision_number(self) -> str:
    return _GetRevisionNumber(self.git_hash)

  @cached_property
  def commit_date(self) -> str:
    return _GetCommitDate(self.git_hash)

  @cached_property
  def to_chromium_version(self) -> 'ChromiumVersion':
    package_json = json.loads(_GetFileAtRevision('package.json', self.git_hash))
    return ChromiumVersion(package_json['config']['projects']['chrome']['tag'])

  # Returns a version like 108.1.48.1
  @cached_property
  def to_combined_version(self) -> str:
    chromium_version = self.to_chromium_version
    return f'{chromium_version.major()}.{self.last_tag[1:]}'


class ChromiumVersion:
  _version: List[int]

  def __init__(self, v: str) -> None:
    super().__init__()
    assert re.match(r'\d+\.\d+\.\d+\.\d+', v)
    self._version = list(map(int, v.split('.')))

  def to_string(self) -> str:
    return '.'.join(map(str, self._version))

  def major(self) -> int:
    return self._version[0]


def _FetchRevision(revision: str):
  args = ['git', 'fetch', 'origin', revision]
  logging.debug('Try to fetch %s' % revision)
  GetProcessOutput(args, cwd=path_util.GetBraveDir())


def _GetFileAtRevision(filepath: str, revision: str) -> str:
  return subprocess.check_output(['git', 'show', f'{revision}:{filepath}'],
                                 cwd=path_util.GetBraveDir())


def _GetCommitDate(revision: str) -> str:
  _, output = GetProcessOutput(['git', 'show', '-s', '--format=%ci', revision],
                               cwd=path_util.GetBraveDir(),
                               check=True)
  commit_date = output.rstrip().split('\n')[-1]
  return commit_date


def _GetGitHash(revision: str) -> Optional[str]:
  result, git_hash_output = GetProcessOutput(
      ['git', 'rev-list', '-n', '1', revision], cwd=path_util.GetBraveDir())
  if not result:
    return None
  return git_hash_output.rstrip()


def _GetRevisionNumber(revision: str) -> str:
  """Returns the number "primary" commits from the begging to `revision`.
  Use this to get the commit from a revision number:
  git rev-list --topo-order --first-parent --reverse origin/master
  | head -n <rev_num> | tail -n 1 | git log -n 1 --stdin
  """

  rev_number_args = [
      'git', 'rev-list', '--topo-order', '--first-parent', '--count', revision
  ]

  _, rev_number_output = GetProcessOutput(rev_number_args,
                                          cwd=path_util.GetBraveDir(),
                                          check=True)

  return rev_number_output.rstrip()
