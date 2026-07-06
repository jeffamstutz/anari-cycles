# Vendor ext (internal): back sphere geometry with Cycles PointCloud

**RESOLVED AS MOOT (2026-07-05):** investigation found sphere geometry already uses
`ccl::PointCloud` (device/Geometry.cpp — `create_node<ccl::PointCloud>()`), so there is
nothing to switch. The performance idea this task proposed is the shipped implementation.
Known PointCloud-related sphere defects were folded into `.todo/35-fix-sphere-defects.md`
(2× radius vs helide; invisible without color attribute; interior-absorption rim artifacts).
No commit associated with this task.
