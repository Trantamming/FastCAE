# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Trần Minh Tâm
import math
import os
import tempfile
import unittest

import fastcae


class TestFastCAE(unittest.TestCase):
    def test_cantilever_matches_theory(self):
        E, L, P = 210e9, 2.0, 1000.0
        sec = fastcae.section("rect", 0.05, 0.1)
        text = fastcae.case(type="frame", E=E, nu=0.3, rho=7850, section="S rect 0.05 0.1", node=["1 0 0 0", "2 2 0 0"],
                            member="1 2 S beam", support="1 fixed", nload="2 0 0 -1000")
        res = fastcae.run(text).raise_for_status()
        exact = P * L ** 3 / (3 * E * sec["Iy"])
        self.assertAlmostEqual(res.values["max_displacement"], exact, delta=1e-9 * exact)
        self.assertEqual(len(res.series["node_uz"]), 2)

    def test_template_and_results(self):
        res = fastcae.run(fastcae.template("simple_beam", 6.0, 10000.0, 8)).raise_for_status()
        self.assertAlmostEqual(res.values["max_moment"], 10000.0 * 36 / 8, delta=1e-6 * 45000)
        self.assertAlmostEqual(res.values["reaction_fz"], 60000.0, delta=1e-6 * 60000)
        self.assertIn("bending_moment", res.fields)

    def test_modal(self):
        text = fastcae.template("cantilever_beam", 3.0, 1000.0, 12) + "analysis = modal\nmodes = 3\n"
        res = fastcae.run(text).raise_for_status()
        self.assertEqual(len(res.series["frequencies_hz"]), 3)
        self.assertGreater(res.values["frequency_1_hz"], 1.0)

    def test_failure_is_reported(self):
        res = fastcae.run("type = frame\n")
        self.assertFalse(res.ok)
        with self.assertRaises(fastcae.FastCAEError):
            res.raise_for_status()

    def test_templates_and_vtk(self):
        names = [t["name"] for t in fastcae.templates()]
        self.assertIn("ibeam", names)
        self.assertIn("warren_truss", names)
        with tempfile.TemporaryDirectory() as d:
            text = fastcae.template("simple_beam", 6.0, 10000.0, 4).replace("output = simple_beam.vtk", "output = out.vtk")
            fastcae.run(text, workdir=d).raise_for_status()
            vtk = fastcae.read_vtk(os.path.join(d, "out.vtk"))
            self.assertGreater(len(vtk["points"]), 10)
            self.assertIn("displacement_magnitude", vtk["scalars"])
            self.assertEqual(len(vtk["scalars"]["displacement_magnitude"]), len(vtk["points"]))
            self.assertIn("displacement", vtk["vectors"])

    def test_lua_bridge(self):
        out = fastcae.run_lua("print(fastcae.author)")
        self.assertIn("Trần Minh Tâm", out)

    @unittest.skipIf(os.name == "nt", "dung solver gia dang shell POSIX")
    def test_external_solver_bridge(self):
        with tempfile.TemporaryDirectory() as d:
            exe = os.path.join(d, "SU2_CFD")
            with open(exe, "w") as f:
                f.write("#!/bin/sh\necho running $1\necho r > history.csv\n")
            os.chmod(exe, 0o755)
            cfg = os.path.join(d, "a.cfg")
            open(cfg, "w").write("SOLVER= EULER\n")
            r = fastcae.su2(cfg, exe=exe, echo=False)
            self.assertTrue(r.ok, r.log)
            self.assertTrue(r.find(".csv"))
            self.assertIn("running", r.log)
            with self.assertRaises(fastcae.FastCAEError):
                fastcae.solver_info("khong_ton_tai")
        self.assertIn("code_aster", [s["id"] for s in fastcae.solvers()])


if __name__ == "__main__":
    unittest.main()
