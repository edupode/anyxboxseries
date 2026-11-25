"""Analisa imports PE com DUMPBIN e compara dependências com o Windows local."""

import argparse
from collections import Counter, defaultdict
import ctypes
from ctypes import wintypes
from dataclasses import dataclass
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import subprocess
import sys
from tempfile import NamedTemporaryFile


MODULE_LINE = re.compile(r" {4}(\S.*)")
NAMED_IMPORT = re.compile(r"([0-9A-Fa-f]{1,4})\s+(\S+)")
ORDINAL_IMPORT = re.compile(r"Ordinal\s+(\d+)")
BOUND_ADDRESS = re.compile(r"^(?:[0-9A-Fa-f]{16}|[0-9A-Fa-f]{8})\s+")

SYSTEM = "System32"
API_SET = "contrato API set implementado"
PACKAGE = "só na build"
MISSING = "ausente na build e no System32"
UNVERIFIED = "contrato API set por verificar"


@dataclass(frozen=True)
class Import:
    module: str
    library: str
    kind: str
    name: str | None = None
    hint: int | None = None
    ordinal: int | None = None

    @property
    def label(self):
        if self.ordinal is not None:
            return f"ordinal {self.ordinal}"
        return f"{self.name} (hint 0x{self.hint:04X})"


def locate_dumpbin(explicit):
    if explicit is not None:
        if not explicit.is_file():
            raise FileNotFoundError(explicit)
        return explicit.resolve()

    on_path = shutil.which("dumpbin.exe")
    if on_path:
        return Path(on_path)

    program_files = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
    vswhere = program_files / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if vswhere.is_file():
        result = subprocess.run([
            str(vswhere), "-all", "-products", "*", "-utf8", "-find",
            r"VC\Tools\MSVC\**\bin\Hostx64\x64\dumpbin.exe"
        ], capture_output=True, encoding="utf-8-sig", text=True, timeout=15, check=True)
        for entry in result.stdout.splitlines():
            path = Path(entry.strip())
            if path.is_file():
                return path

    raise FileNotFoundError("DUMPBIN não encontrado. Indica o executável com --dumpbin.")


def parse_imports(module, output):
    kind = None
    library = None
    imports = []

    for line in output.splitlines():
        text = line.strip()
        if text == "Section contains the following imports:":
            kind, library = "normal", None
            continue
        if text in ("Section contains the following delay load imports:",
                    "Section contains the following delay-load imports:"):
            kind, library = "delay-load", None
            continue
        if text == "Summary":
            break
        if not kind or not text:
            continue

        module_line = MODULE_LINE.fullmatch(line.rstrip())
        if module_line:
            library = module_line[1].casefold()
            continue
        if library is None or not line.startswith(" " * 24):
            continue

        if kind == "delay-load":
            text = BOUND_ADDRESS.sub("", text)
        ordinal = ORDINAL_IMPORT.fullmatch(text)
        if ordinal:
            imports.append(Import(module, library, kind, ordinal=int(ordinal[1])))
            continue
        named = NAMED_IMPORT.fullmatch(text)
        if named:
            imports.append(Import(module, library, kind, name=named[2], hint=int(named[1], 16)))
            continue
        raise ValueError(f"Linha de símbolo DUMPBIN inesperada em {library} · {text}")

    return imports


def get_imports(dumpbin, root, file):
    env = dict(os.environ, VSLANG="1033")
    result = subprocess.run([str(dumpbin), "/nologo", "/imports", str(file)],
                            capture_output=True, text=True, encoding="mbcs",
                            env=env, timeout=45)
    if result.returncode or "File Type:" not in result.stdout:
        message = result.stderr.strip() or result.stdout.strip()
        raise ValueError(message.splitlines()[-1] if message else f"DUMPBIN saiu com {result.returncode}")
    return parse_imports(str(file.relative_to(root)), result.stdout)


def load_api_set_query():
    try:
        query = ctypes.WinDLL("api-ms-win-core-apiquery-l2-1-0.dll",
                              winmode=0x800).IsApiSetImplemented
    except (OSError, AttributeError):
        return None
    query.argtypes = [ctypes.c_char_p]
    query.restype = wintypes.BOOL
    return query


def classify(library, system32, local_files, api_set_query):
    is_api_set = library.startswith(("api-ms-", "ext-ms-")) and library.endswith(".dll")
    if is_api_set and api_set_query is not None and api_set_query(library[:-4].encode("ascii")):
        return API_SET

    if PureWindowsPath(library).name == library and (system32 / library).is_file():
        return SYSTEM
    if library in local_files:
        return PACKAGE
    if is_api_set and api_set_query is None:
        return UNVERIFIED
    return MISSING


def render_report(root, executable, main_present, system32, dumpbin, scan, failures, libraries, states):
    build = sys.getwindowsversion().build
    header = [
        "Passo 1 · Importações PE/COFF",
        "",
        "Pasta da build",
        f"  {root}",
        "Executável principal",
        f"  {executable.relative_to(root)} · {'presente' if main_present else 'em falta'}",
        "",
        f"Windows build {build} · {system32}",
        f"DUMPBIN · {dumpbin}",
        "A verificação cobre a tabela normal e a tabela delay-load de cada ficheiro legível.",
        "A disponibilidade refere-se à build, ao System32 e aos contratos API set do PC.",
        "",
        "Ficheiros analisados",
    ]
    for module, imports in scan.items():
        delayed = sum(item.kind == "delay-load" for item in imports)
        header.append(f"  {module} · {len(imports) - delayed} normais, {delayed} delay-load")
    for file, error in failures.items():
        header.append(f"  {file} · erro de leitura · {error}")

    count = Counter(states.values())
    lines = header + [
        "",
        f"{len(libraries)} dependências únicas · {sum(len(items) for items in libraries.values())} importações",
        f"  {count[SYSTEM]} com ficheiro em System32",
        f"  {count[API_SET]} contratos API set implementados",
        f"  {count[PACKAGE]} disponíveis só na build",
        f"  {count[MISSING]} ausentes na build e no System32",
        f"  {count[UNVERIFIED]} contratos por verificar",
        "",
        "Disponibilidade por dependência",
    ]
    for library, items in sorted(libraries.items()):
        lines.append(f"  {library} · {states[library]} · {len(items)} importações")

    lines.extend(["", "Símbolos e ordinais das dependências fora do System32/API set"])
    for library, items in sorted(libraries.items()):
        if states[library] in (SYSTEM, API_SET):
            continue
        lines.append(f"  {library} · {states[library]}")
        for item in items:
            lines.append(f"    {item.module} · {item.kind} · {item.label}")

    lines.extend(["", "Importações por ordinal no Windows"])
    for library, items in sorted(libraries.items()):
        if states[library] not in (SYSTEM, API_SET):
            continue
        for item in items:
            if item.ordinal is not None:
                lines.append(f"  {item.module} · {library} · {item.kind} · {item.label}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pasta", type=Path, help="Pasta com a build Xbox")
    parser.add_argument("executavel", type=Path, help="Nome ou caminho relativo do PE principal")
    parser.add_argument("--saida", type=Path, default=Path(__file__).with_name("imports-xbox.txt"))
    parser.add_argument("--dumpbin", type=Path, help="Caminho para o DUMPBIN do Visual Studio")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("O diagnóstico compara as dependências com um Windows local.")
    root = args.pasta.resolve()
    if not root.is_dir():
        parser.error(f"Pasta inexistente · {root}")
    executable = (root / args.executavel).resolve()
    if not executable.is_relative_to(root):
        parser.error("O executável deve estar dentro da pasta da build.")

    dumpbin = locate_dumpbin(args.dumpbin)
    is_wow64 = ctypes.sizeof(ctypes.c_void_p) == 4 and bool(os.environ.get("PROCESSOR_ARCHITEW6432"))
    system_folder = "Sysnative" if is_wow64 else "System32"
    system32 = Path(os.environ["SystemRoot"]) / system_folder
    if not system32.is_dir():
        parser.error(f"Pasta do sistema indisponível · {system32}")

    main_present = executable.is_file()
    dlls = sorted(file for file in root.rglob("*.dll") if file.is_file())
    local_files = {dll.name.casefold() for dll in dlls}
    files = ([executable] if main_present and executable not in dlls else []) + dlls
    libraries = defaultdict(list)
    scan = {}
    failures = {}
    for file in files:
        relative = str(file.relative_to(root))
        try:
            scan[relative] = get_imports(dumpbin, root, file)
        except (OSError, ValueError, subprocess.TimeoutExpired) as error:
            failures[relative] = str(error)
        else:
            for record in scan[relative]:
                libraries[record.library].append(record)

    api_set_query = load_api_set_query()
    states = {name: classify(name, system32, local_files, api_set_query) for name in libraries}
    report = render_report(root, executable, main_present, system32, dumpbin,
                           scan, failures, libraries, states)
    destination = args.saida.resolve()
    with NamedTemporaryFile(mode="w", encoding="utf-8", dir=destination.parent, delete=False) as output:
        temporary = Path(output.name)
        output.write(report)
    try:
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)

    unavailable = [name for name, state in states.items() if state == MISSING]
    print(f"Ficheiros analisados: {len(scan)}. Erros de leitura: {len(failures)}.")
    print("Executavel principal: " + ("presente." if main_present else "em falta."))
    print("Dependencias ausentes: " + (", ".join(sorted(unavailable)) or "nenhuma"))
    print(f"Relatorio: {destination}")
    return int(bool(failures))


if __name__ == "__main__":
    sys.exit(main())
