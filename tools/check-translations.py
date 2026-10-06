#!/usr/bin/env python3
"""Refuses a build that would show a string nobody translated.

Two things are checked, both fatal:

  * every text the interface puts on screen goes through qsTr()/tr(), so a
    literal written straight into a visible property is an error;
  * every string those calls name is in the catalog, in every language.
"""
import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CATALOG = ROOT / "app" / "i18n" / "messages.json"
SOURCE_LANGUAGE = "en"

# Properties and calls that put text in front of a reader.
QML_TEXT_PROPERTIES = (
    "text", "placeholderText", "placeholder", "title", "label", "description", "acceptText",
    "rejectText", "cancelText", "toolTipText", "unitLabel", "emptyText", "hint", "caption",
    "reason", "failure", "unavailable", r"Accessible\.name", r"ToolTip\.text",
)
QML_TEXT_CALLS = ("showError", "showToast", "showInfo")

# Members and variables whose value is read out as prose later.
CPP_TEXT_FIELDS = ("title", "status", "text", "summary", "connectPhase_", "servingKeyStage_",
    "status_", "preview")

# Signals and helpers whose argument reaches the user as prose.
CPP_TEXT_CALLS = (
    "actionFailed", "actionOk", "createFailed", "accountOpenFailed", "restartRequired",
    "writeConversationNote", "opDone", "opProgress", "opBegin", "beginOperation",
    "updateOperation", "finishOperation", "sendResult", "contactAddDone", "openFailed",
    "accountClosed", "servingKeyDone", "servingKeyStage", "aliasActivationDone",
    "contactAddStage", "downloadFinished", "connectProgress", "beginOp", "succeed", "fail",
    "beginStorageWork", "settleUnfinishedNotes", "writeContactProgress", "aliasHoldings",
)

STRING = r'"(?:[^"\\]|\\.)*"'
# A sentence may be written as several literals joined over as many lines as it
# needs; the catalog knows it as the one sentence they make.
# QML joins with +, C++ by writing one literal after another.
JOINED = STRING + r'(?:\s*\+?\s*' + STRING + r')*'
TRANSLATED = re.compile(
    r'\b(?:qsTr|qsTranslate|tr|QT_TR_NOOP)\s*\(\s*(' + JOINED + r')\s*'
    r'(?:,\s*(' + STRING + r')\s*)?[,)]', re.S)
QML_ASSIGNMENT = re.compile(
    r'(?<!property color )(?<!property int )(?<!property real )'
    r'(?:(?<![\w.])|(?<=\.))(' + "|".join(QML_TEXT_PROPERTIES) + r')\s*:\s*([^\n]*)')
# Written from a handler, not bound: the same text, set by hand.
QML_SET = re.compile(r'(?:(?<![\w.])|(?<=\.))('
    + "|".join(QML_TEXT_PROPERTIES) + r')\s*=\s*([^\n]*)')
QML_CALL = re.compile(r'\b(?:' + "|".join(QML_TEXT_CALLS) + r')\s*\(\s*(' + STRING + r')')
CPP_FIELD = re.compile(r'(?:\.|->|\b)(' + "|".join(CPP_TEXT_FIELDS)
    + r')\s*=\s*([^\n]*)')
CPP_CALL = re.compile(r'\b(?:' + "|".join(CPP_TEXT_CALLS) + r')\s*\(([^;]*?)\)\s*;', re.S)
BARE_STRING = re.compile(STRING)
# A literal that is not prose: an id, a colour, a path, a format, a qrc url.
NOT_PROSE = re.compile(r'^"(?:|[^a-zA-Z]*|[\w./:%@+-]*|#[0-9a-fA-F]+|qrc:.*|image://.*)"$')
# Drawing instructions, not words: the shape of an icon.
SVG_PATH = re.compile(r'^[MmLlHhVvCcSsQqTtAaZz][\sMmLlHhVvCcSsQqTtAaZz0-9.,-]*$')
# What the code calls things: an icon name, a state, a type.
CODE_WORD = re.compile(r'^[a-z0-9_.:-]+$')
# A sentence may be returned in pieces, and a choice returns one of several.
RETURNED = re.compile(r'\breturn\s+(' + JOINED + r')')
# Inside a visible property even one word is prose; only these are not.
NOT_SHOWN = re.compile(r'^"(?:|[^a-zA-Z]*|#[0-9a-fA-F]+|qrc:.*|image://.*|\w+://.*)"$')
# Compared against, not shown: a transport name, a date format Qt reads.
NOT_TEXT = ("gateway", "embedded", "sam", "hh:mm", "yyyy-MM-dd", "dd MMM hh:mm",
    "dddd, d MMMM yyyy, hh:mm:ss")
# Shown, and deliberately the same in every language.
UNTRANSLATED = ("min", "middle", "max")


def unquote(literal):
    if not literal.startswith('"'):
        return literal
    return "".join(one_string(part) for part in re.findall(STRING, literal))


def one_string(part):
    try:
        return json.loads(part)
    except json.JSONDecodeError:
        # C++ writes bytes as \x escapes, which JSON has no idea about.
        raw = part[1:-1].encode("ascii", "replace").decode("unicode_escape")
        return raw.encode("latin-1", "replace").decode("utf-8", "replace")


def is_prose(literal, shown=False):
    if (NOT_SHOWN if shown else NOT_PROSE).match(literal):
        return False
    if SVG_PATH.match(unquote(literal)):
        return False
    if unquote(literal) in NOT_TEXT or unquote(literal) in UNTRANSLATED:
        return False
    body = unquote(literal)
    return any(c.isalpha() for c in body) and len(body) > 1


def read_catalog():
    document = json.loads(CATALOG.read_text(encoding="utf-8"))
    languages = [entry["code"] for entry in document["languages"]]
    wanted = [code for code in languages if code != SOURCE_LANGUAGE]
    return document["messages"], wanted


def used_strings(problems):
    used = {}
    for path in sorted(ROOT.glob("app/**/*.qml")) + sorted(ROOT.glob("app/*.cpp")) \
            + sorted(ROOT.glob("app/*.hpp")):
        text = path.read_text(encoding="utf-8")
        for match in TRANSLATED.finditer(text):
            key = unquote(match.group(1))
            if match.group(2):
                key += "|" + unquote(match.group(2))
            used.setdefault(key, set()).add(path.name)
        if path.suffix == ".qml":
            check_qml(path, text, problems)
        else:
            check_cpp(path, text, problems)
    return used


def line_of(text, index):
    return text.count("\n", 0, index) + 1


# A value may run over several lines: the branches of a choice and the pieces of
# a sentence are written under the property, not beside it.
CONTINUES = re.compile(r'^\s*[?:+.]')


def depth_after(line, depth):
    for piece in re.split(STRING, line):
        depth += piece.count("(") + piece.count("[") + piece.count("{")
        depth -= piece.count(")") + piece.count("]") + piece.count("}")
    return depth


def value_of(text, start):
    lines = text[start:].split("\n")
    value = [lines[0]]
    depth = depth_after(lines[0], 0)
    for line in lines[1:]:
        if depth <= 0 and not CONTINUES.match(line):
            break
        value.append(line)
        depth = depth_after(line, depth)
    return "\n".join(value)


def check_qml(path, text, problems):
    for match in list(QML_ASSIGNMENT.finditer(text)) + list(QML_SET.finditer(text)):
        value = value_of(text, match.start(2))
        # What is already wrapped is settled; what is left beside it is not.
        without = re.sub(
            r'\b(?:qsTr|qsTranslate)\s*\(\s*' + JOINED + r'\s*(?:,\s*' + STRING + r'\s*)?\)',
            "", value, flags=re.S)
        # A value a state is compared against is never read by anybody.
        without = re.sub(r'[!=]==?\s*' + STRING, "", without)
        without = re.sub(r'\bcase\s+' + STRING + r'\s*:', "", without)
        for literal in BARE_STRING.findall(without):
            if is_prose(literal, shown=True):
                problems.append(
                    f"{path.relative_to(ROOT)}:{line_of(text, match.start())}: "
                    f"{match.group(1)} is given {literal} without qsTr()")
    for match in QML_CALL.finditer(text):
        if is_prose(match.group(1), shown=True):
            problems.append(
                f"{path.relative_to(ROOT)}:{line_of(text, match.start())}: "
                f"{match.group(1)} shown to the user without qsTr()")
    check_returns(path, text, problems, "qsTr()")


# What a function hands back is read by whoever shows it.
def check_returns(path, text, problems, call):
    for match in RETURNED.finditer(text):
        literal = match.group(1)
        if BARE_STRING.match(literal) is None:
            continue
        # An icon or state code is written in lowercase; a label is not.
        if CODE_WORD.match(unquote(literal)) or not is_prose(literal, shown=True):
            continue
        problems.append(
            f"{path.relative_to(ROOT)}:{line_of(text, match.start())}: "
            f"{literal} is returned to be shown without {call}")


def check_cpp(path, text, problems):
    for match in CPP_FIELD.finditer(text):
        value = value_of(text, match.start(2))
        without = re.sub(
            r'\b(?:tr|QT_TR_NOOP)\s*\(\s*' + JOINED + r'\s*(?:,\s*' + STRING + r'\s*)?\)',
            "", value, flags=re.S)
        without = re.sub(r'[!=]==?\s*' + STRING, "", without)
        for literal in BARE_STRING.findall(without):
            if is_prose(literal):
                problems.append(
                    f"{path.relative_to(ROOT)}:{line_of(text, match.start())}: "
                    f"{match.group(1)} is given {literal} without tr()")
    check_returns(path, text, problems, "tr()")
    for match in CPP_CALL.finditer(text):
        arguments = match.group(1)
        without = re.sub(r'\btr\s*\(\s*' + JOINED + r'\s*(?:,\s*' + STRING + r'\s*)?\)',
            "", arguments)
        for literal in BARE_STRING.findall(without):
            if is_prose(literal):
                problems.append(
                    f"{path.relative_to(ROOT)}:{line_of(text, match.start())}: "
                    f"{literal} shown to the user without tr()")


def main():
    problems = []
    used = used_strings(problems)
    messages, wanted = read_catalog()

    for key in sorted(used):
        entry = messages.get(key)
        if entry is None:
            where = ", ".join(sorted(used[key]))
            problems.append(
                f"{where}: {key!r} is shown but the catalog does not carry it - add it to "
                f"{CATALOG.relative_to(ROOT)}")
            continue
        slots = set(re.findall(r'%\d', key))
        for code in wanted:
            text = entry.get(code, "")
            if not text.strip():
                where = ", ".join(sorted(used[key]))
                problems.append(
                    f"{CATALOG.relative_to(ROOT)}: {key!r} has no {code} translation, and "
                    f"{where} shows it")
                continue
            if set(re.findall(r'%\d', text)) != slots:
                problems.append(
                    f"{CATALOG.relative_to(ROOT)}: the {code} text of {key!r} does not carry "
                    f"the same values as the source - {text!r}")

    for key in sorted(set(messages) - set(used)):
        problems.append(
            f"{CATALOG.relative_to(ROOT)}: {key!r} is translated but no source shows it - "
            f"remove it from the catalog")

    if problems:
        print(f"translations: {len(problems)} problem(s); the interface would show a string "
              "nobody translated", file=sys.stderr)
        for problem in problems:
            print("  " + problem, file=sys.stderr)
        return 1
    print(f"translations: {len(used)} strings, {len(wanted)} languages, all present")
    return 0


if __name__ == "__main__":
    sys.exit(main())
