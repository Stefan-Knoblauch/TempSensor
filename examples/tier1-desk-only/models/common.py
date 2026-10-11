"""Shared helpers: a check record and markdown table rendering."""
from dataclasses import dataclass


@dataclass
class Check:
    req: str          # requirement ID, e.g. "T1-07"
    what: str         # what was checked
    value: str        # computed value (formatted)
    limit: str        # acceptance limit (formatted)
    ok: bool

    @property
    def verdict(self) -> str:
        return "PASS" if self.ok else "FAIL"


def md_table(header, rows) -> str:
    out = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out)


def checks_table(checks) -> str:
    return md_table(["Req", "Check", "Value", "Limit", "Result"],
                    [(c.req, c.what, c.value, c.limit, c.verdict) for c in checks])
