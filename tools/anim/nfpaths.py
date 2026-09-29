"""Where the animation tools find the game data. Each can be overridden with an environment variable.

DISC  the Eurocom archives, filesys.dNN (NF_DISC, a pattern with %02d)
LVL   the level bundles extract_all.py writes, and the skeletons/skins/scripts it pulls out of them (NF_ANIM_LVL)
DUMP  the files psiFileLoad writes to dump/ when the game runs (NF_ANIM_DUMP)
"""
import os

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
DISC = os.environ.get('NF_DISC', os.path.join(REPO, 'disc', 'eurocom', 'filesys.d%02d'))
LVL = os.environ.get('NF_ANIM_LVL', os.path.join(REPO, 'build', 'anim', 'lvl'))
DUMP = os.environ.get('NF_ANIM_DUMP', os.path.join(REPO, 'build', 'windows', 'Release', 'dump'))
