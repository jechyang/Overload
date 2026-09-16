"""Keep diagnostic paths repository-relative while putting new evidence off-volume."""
import hashlib
import os
from pathlib import Path
import subprocess
def evidence_root(root):
    parent = root / 'Build/Tests/Reflection'
    parent.mkdir(parents=True, exist_ok=True)
    requested = os.environ.get('OV_REFLECTION_EVIDENCE_ROOT')
    if not requested:
        return parent
    physical = Path(requested).resolve()
    physical.mkdir(parents=True, exist_ok=True)
    if physical == parent.resolve():
        return parent
    # The generator reports source paths relative to the repository. A junction
    # keeps negative-test locations meaningful without filling the source drive.
    link = parent / ('external-' + hashlib.sha256(str(physical).encode()).hexdigest()[:12])
    if not link.exists():
        subprocess.run(['cmd.exe', '/d', '/c', 'mklink', '/J', str(link), str(physical)], check=True, stdout=subprocess.DEVNULL)
    if link.resolve() != physical:
        raise RuntimeError('Evidence junction points at an unexpected directory')
    return link
