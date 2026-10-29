# Convert a FreeCAD document (.FCStd) to STEP so FastCAE can import it.
#
#   freecadcmd tools/fcstd_to_step.py  model.FCStd  model.step
#
# Only top-level solid bodies (objects that are not used by another object) are exported.
import sys

import FreeCAD
import Import


def main(src, dst):
    doc = FreeCAD.openDocument(src)
    shapes = []
    for obj in doc.Objects:
        if not hasattr(obj, "Shape") or obj.Shape.isNull():
            continue
        if obj.InList:  # consumed by another feature (e.g. sketch inside a Pad)
            continue
        if obj.Shape.Solids:
            shapes.append(obj)
    if not shapes:
        raise SystemExit("No solid bodies found in " + src)
    Import.export(shapes, dst)
    print("Exported %d object(s) to %s" % (len(shapes), dst))


# freecadcmd passes script arguments after the script name.
args = [a for a in sys.argv if not a.lower().endswith(("freecadcmd", "freecadcmd.exe", ".py"))]
if len(args) < 2:
    raise SystemExit("usage: freecadcmd fcstd_to_step.py input.FCStd output.step")
main(args[-2], args[-1])
