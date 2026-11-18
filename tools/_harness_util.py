"""Shared helpers for the tools/test_*.py host regressions.

Several tests compile a production C function in isolation by copying its text
out of the source file. Keep that extraction in one place so every test agrees
on what "the function" is.
"""


def c_function(text, signature):
    """Return the C definition that starts at the first occurrence of
    ``signature`` and ends at its matching closing brace.

    Braces are counted naively (no awareness of strings or comments), exactly as
    the per-test copies did; production functions extracted this way must keep
    their braces balanced in comments and literals.
    """
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
