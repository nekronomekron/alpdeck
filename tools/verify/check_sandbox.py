"""Sandbox conformance: what a Lua app can and cannot reach.

Invariant 3 of the project: no io, no os, no package -- fs.* is the only route
to storage and every path passes the sandbox check. This asserts that against
the same restricted _ENV the firmware builds.

Checks are split into two lists. `REQUIRED` must pass; `KNOWN_ISSUES` are
policy violations that exist today, reported every run so they cannot be
forgotten, and promoted to hard failures with --strict once fixed.
"""

import luaenv

# name -> (lua expression, expected result, description)
REQUIRED = [
    ("os absent", "os == nil", True),
    ("io absent", "io == nil", True),
    ("package absent", "package == nil", True),
    ("require absent", "require == nil", True),
    ("debug absent", "debug == nil", True),
    ("coroutine absent", "coroutine == nil", True),
    ("utf8 absent", "utf8 == nil", True),
    ("math present", "type(math) == 'table' and math.floor(2.7) == 2", True),
    ("string present", "type(string) == 'table' and string.format('%d', 7) == '7'", True),
    ("table present", "type(table) == 'table' and #({1,2,3}) == 3", True),
    ("pcall present", "type(pcall) == 'function'", True),
    ("display table", "type(display) == 'table' and type(display.text) == 'function'", True),
    ("input table", "type(input) == 'table' and type(input.read) == 'function'", True),
    ("fs table", "type(fs) == 'table' and type(fs.read) == 'function'", True),
    ("sys table", "type(sys) == 'table' and type(sys.info) == 'function'", True),
    ("wifi table", "type(wifi) == 'table' and type(wifi.status) == 'function'", True),
    ("ftp table", "type(ftp) == 'table' and type(ftp.configure) == 'function'", True),
    # The move out of sys is the whole point of api 2; a leftover would mean
    # two ways to do the same thing, one of them undocumented.
    ("sys.wifi gone", "sys.wifi == nil and sys.wifi_scan == nil", True),
    ("sys.ftp_configure gone", "sys.ftp_configure == nil", True),
    ("_G is the sandbox", "_G.display ~= nil and _G.os == nil", True),
    # luaopen_base installs these and they use C stdio, not fs.*; both storage
    # mounts are visible to the ESP VFS, so they were a way around the path
    # sandbox. LuaWrapper nils them out after opening the base library.
    ("dofile absent", "dofile == nil", True),
    ("loadfile absent", "loadfile == nil", True),
]

KNOWN_ISSUES = []


def _evaluate(expression):
    """Run one expression inside the sandbox and return its value."""
    host = luaenv.Host()
    source = "RESULT = (%s)" % expression

    # The probe cannot hand a value to Python directly, so it stores one in the
    # sandbox and the caller reads it back out of the environment table.
    lua_holder = []
    ok, error = _run_capturing(source, host, lua_holder)
    if not ok:
        raise AssertionError("sandbox probe failed: %s" % error)
    return lua_holder[0]


def _run_capturing(source, host, sink):
    from lupa import LuaRuntime

    lua = LuaRuntime(unpack_returned_tuples=True)
    host.lua = lua
    env = luaenv.build_environment(lua, host)

    # tostring(err) keeps the return arity at two: lupa cannot unpack a single
    # value, and a successful load returns only the function.
    loader = lua.eval(
        "function(src, name, env) local f, e = load(src, name, 't', env)"
        " return f, tostring(e) end"
    )
    chunk, error = loader(source, "@probe", env)
    if chunk is None:
        return False, str(error)
    try:
        chunk()
    except Exception as failure:
        return False, str(failure)
    sink.append(env["RESULT"])
    return True, None


def run(strict=False):
    """Returns (passed, failed, issues) counts and prints a report."""
    passed = failed = 0

    for name, expression, expected in REQUIRED:
        actual = _evaluate(expression)
        if actual == expected:
            passed += 1
        else:
            failed += 1
            print("  FAIL  %-22s %s -> %r (expected %r)" % (name, expression, actual, expected))

    issues = 0
    for name, expression, expected, note in KNOWN_ISSUES:
        actual = _evaluate(expression)
        if actual == expected:
            passed += 1
            continue
        if strict:
            failed += 1
            print("  FAIL  %-22s %s" % (name, note))
        else:
            issues += 1
            print("  KNOWN %-22s %s" % (name, note))

    print("  sandbox: %d passed, %d failed, %d known issues" % (passed, failed, issues))
    return passed, failed, issues


if __name__ == "__main__":
    import sys

    _, fails, _ = run(strict="--strict" in sys.argv)
    raise SystemExit(1 if fails else 0)
