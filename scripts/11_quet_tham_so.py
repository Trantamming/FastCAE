# FastCAE - Python: quet tham so bang solver khung 1D noi tai, so voi ly thuyet 5qL^4/(384EI).
import fastcae

L, q, b, n, E = 6.0, 12000.0, 0.15, 8, 210e9
print("%6s %14s %14s %8s" % ("h (m)", "vong FE (mm)", "ly thuyet (mm)", "lech %"))
for k in range(6):
    h = 0.20 + 0.05 * k
    text = fastcae.case(
        type="frame", E=E, nu=0.3, rho=7850, section="S rect %g %g" % (b, h),
        node=["%d %g 0 0" % (i + 1, L * i / n) for i in range(n + 1)],
        member=["%d %d S beam" % (i + 1, i + 2) for i in range(n)],
        udl=["%d 0 0 %g" % (i + 1, -q) for i in range(n)],
        support=["1 x y z rx", "%d y z" % (n + 1)])
    res = fastcae.run(text).raise_for_status()
    exact = 5 * q * L ** 4 / (384 * E * b * h ** 3 / 12)
    fe = res.values["max_displacement"]
    print("%6.2f %14.3f %14.3f %8.4f" % (h, fe * 1000, exact * 1000, 100 * abs(fe - exact) / exact))
