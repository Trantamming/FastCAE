# FastCAE - Python: liet ke va goi cac solver ma nguon mo (Code_Aster, OpenRadioss, SU2, OpenFOAM, CalculiX, Elmer).
# Bam Chay (F6). Solver nao chua cai se duoc bo qua kem huong dan. Dat duong dan trong Cong cu > Quan ly solver
# hoac bien moi truong (FASTCAE_ASTER_BIN, FASTCAE_SU2_BIN, FASTCAE_RADIOSS_BIN, FASTCAE_OPENFOAM_BIN, FASTCAE_CCX).
import os
import fastcae

print("=== Solver ma nguon mo ===")
for s in fastcae.solvers():
    if s["runnable"]:
        print("%-12s %-8s %-22s %s" % (s["id"], "DA CAI" if s["installed"] else "chua cai", s["license"], s["path"]))

work = os.path.join(os.path.expanduser("~"), "FastCAE_runs")
os.makedirs(work, exist_ok=True)

# Mau goi tung solver (bo dau # va sua duong dan file input cua ban):
#   r = fastcae.code_aster(r"D:\ca\model.export")                  # Code_Aster: run_aster model.export
#   r = fastcae.openradioss(r"D:\rad\model_0000.rad", args="")      # starter roi engine
#   r = fastcae.su2(r"D:\su2\inv_naca0012.cfg")                     # SU2_CFD
#   r = fastcae.openfoam(r"D:\foam\pitzDaily", ["blockMesh", "simpleFoam", "foamToVTK"])
#   r = fastcae.calculix(r"D:\ccx\beam.inp")
#   print(r.ok, r.find(".vtk", ".frd", ".rmed", ".csv"))           # file ket qua moi tao

# Vi du chay that voi CalculiX neu da cai: tao deck 1 phan tu va giai
if fastcae.solver_info("calculix")["installed"]:
    inp = os.path.join(work, "demo.inp")
    with open(inp, "w") as f:
        f.write("*NODE\n1,0,0,0\n2,1,0,0\n3,1,1,0\n4,0,1,0\n5,0,0,1\n6,1,0,1\n7,1,1,1\n8,0,1,1\n"
                "*ELEMENT,TYPE=C3D8,ELSET=E\n1,1,2,3,4,5,6,7,8\n*MATERIAL,NAME=M\n*ELASTIC\n210e9,0.3\n"
                "*SOLID SECTION,ELSET=E,MATERIAL=M\n*BOUNDARY\n1,1,3\n4,1\n4,3\n5,1,2\n8,1\n"
                "*STEP\n*STATIC\n*CLOAD\n6,3,1000\n7,3,1000\n*NODE FILE\nU\n*END STEP\n")
    r = fastcae.calculix(inp, work)
    print("Ket qua:", r.ok, r.find(".frd"))
else:
    print("(CalculiX chua cai: bo qua vi du chay that)")
