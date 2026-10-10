"""Shared paths for the research tools. Override with the M4_APP environment variable."""
import os
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
M4_APP = os.environ.get('M4_APP', os.path.expanduser(
    '~/Library/Application Support/Steam/steamapps/common/Myst IV Revelation/Myst IV Revelation.app'))
CX = os.path.join(M4_APP, 'Contents', 'SharedSupport', 'myst_iv')
GAME_BIN = os.path.join(CX, 'support', 'myst_iv', 'drive_c', 'Myst 4', 'bin')
GAME_DATA = os.path.join(CX, 'support', 'myst_iv', 'drive_c', 'Myst 4', 'data')
