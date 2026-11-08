from __future__ import annotations

import unittest


def looks_absolute_or_drive(path: str) -> bool:
    if not path:
        return True
    if path[0] in "/\\":
        return True
    if len(path) >= 2 and path[0].isalpha() and path[1] == ":":
        return True
    return False


def has_traversal(path: str) -> bool:
    parts = path.replace("\\", "/").split("/")
    return any(part in ("", ".", "..") for part in parts)


def prescan_paths(lines: list[str], *, listing_truncated: bool = False) -> tuple[bool, str]:
    if listing_truncated:
        return False, "prescan.listing_overflow"
    for original in lines:
        path = original.rstrip("/\\")
        if not path:
            return False, "prescan.empty_path"
        if looks_absolute_or_drive(path) or has_traversal(path):
            return False, "prescan.traversal"
    return True, "ok"


class PrescanParityTests(unittest.TestCase):
    def test_rejects_zip_slip_and_absolute(self) -> None:
        ok, code = prescan_paths(["../../etc/passwd"])
        self.assertFalse(ok)
        self.assertEqual(code, "prescan.traversal")
        ok, code = prescan_paths(["/tmp/evil"])
        self.assertFalse(ok)
        self.assertEqual(code, "prescan.traversal")
        ok, code = prescan_paths(["C:/Windows/system32"])
        self.assertFalse(ok)
        self.assertEqual(code, "prescan.traversal")

    def test_fail_closed_on_truncated_listing(self) -> None:
        ok, code = prescan_paths(["safe/file.txt"], listing_truncated=True)
        self.assertFalse(ok)
        self.assertEqual(code, "prescan.listing_overflow")

    def test_accepts_relative_safe_paths(self) -> None:
        ok, code = prescan_paths(["pkg-1.0.0/pafio.toml", "pkg-1.0.0/src/lib.styio"])
        self.assertTrue(ok)
        self.assertEqual(code, "ok")


if __name__ == "__main__":
    unittest.main()
