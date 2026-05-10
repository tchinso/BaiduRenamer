import locale
import os
import queue
import re
import subprocess
import sys
import threading
import traceback
from dataclasses import dataclass, field
from pathlib import Path
from tkinter import filedialog, messagebox
import tkinter as tk
from tkinter import ttk


APP_NAME = "BaiduRenamer"
DEFAULT_DOWNLOAD_DIR = r"J:\BaiduNetdiskDownload"
DEFAULT_BANDIZIP = r"C:\Program Files\Bandizip\bz.exe"
DEFAULT_PASSWORD = "somisoft"

ARCHIVE_EXTENSIONS = {".7z", ".zip", ".zi", ".001", ".rar"}
SECOND_PASS_EXTENSIONS = {".zi", ".zip"}
PART_RAR_RE = re.compile(r"(^|[._ -])part(?P<number>\d+)$", re.IGNORECASE)


@dataclass
class RenameStats:
    scanned: int = 0
    changed: int = 0
    skipped: int = 0
    failed: int = 0
    error_files: list[str] = field(default_factory=list)


@dataclass
class ExtractStats:
    first_targets: int = 0
    first_success: int = 0
    first_failed: int = 0
    second_targets: int = 0
    second_success: int = 0
    second_failed: int = 0
    error_files: list[str] = field(default_factory=list)


def normalize_key(path: Path) -> str:
    return os.path.normcase(os.path.abspath(str(path)))


def has_exe_suffix(path: Path) -> bool:
    return any(suffix.lower() == ".exe" for suffix in path.suffixes)


def suffix_without_dot(suffix: str) -> str:
    return suffix[1:].lower() if suffix.startswith(".") else suffix.lower()


def extension_change_target(path: Path):
    suffixes = path.suffixes

    if has_exe_suffix(path):
        return None, "exe 파일"

    if len(suffixes) >= 2:
        front_ext = suffix_without_dot(suffixes[-2])
        if front_ext.startswith(("7", "r")):
            return path.with_suffix(""), "이중확장자 뒤 확장자 제거"
        if front_ext.startswith("0"):
            return None, "0으로 시작하는 이중확장자"

    if not suffixes:
        return path.with_name(path.name + ".zip"), "zip 확장자 추가"

    last_ext = suffix_without_dot(suffixes[-1])
    if last_ext.startswith("0"):
        return None, "0으로 시작하는 확장자"
    if last_ext.startswith("7"):
        return path.with_suffix(".7z"), "7z로 변경"
    if last_ext.startswith("r"):
        return path.with_suffix(".rar"), "rar로 변경"
    return path.with_suffix(".zip"), "zip으로 변경"


def iter_files(root: Path, recursive: bool, log):
    if not recursive:
        try:
            children = sorted(root.iterdir(), key=lambda item: item.name.lower())
        except OSError as exc:
            log(f"폴더 접근 실패: {root} ({exc})")
            return

        for child in children:
            if child.is_file():
                yield child
        return

    stack = [root]
    while stack:
        current = stack.pop()
        try:
            with os.scandir(current) as scan:
                entries = sorted(list(scan), key=lambda item: item.name.lower())
        except OSError as exc:
            log(f"폴더 접근 실패: {current} ({exc})")
            continue

        for entry in entries:
            entry_path = Path(entry.path)
            try:
                if entry.is_dir(follow_symlinks=False):
                    stack.append(entry_path)
                elif entry.is_file(follow_symlinks=False):
                    yield entry_path
            except OSError as exc:
                log(f"파일 확인 실패: {entry_path} ({exc})")


def iter_directories(root: Path, log):
    stack = [root]
    while stack:
        current = stack.pop()
        yield current
        try:
            with os.scandir(current) as scan:
                entries = sorted(list(scan), key=lambda item: item.name.lower())
        except OSError as exc:
            log(f"폴더 접근 실패: {current} ({exc})")
            continue

        for entry in entries:
            try:
                if entry.is_dir(follow_symlinks=False):
                    stack.append(Path(entry.path))
            except OSError as exc:
                log(f"폴더 확인 실패: {entry.path} ({exc})")


def rename_without_overwrite(source: Path, target: Path, log) -> bool:
    if normalize_key(source) == normalize_key(target):
        log(f"건너뜀: 이미 대상 이름입니다 - {source}")
        return False
    if target.exists():
        raise FileExistsError(f"대상 파일이 이미 있습니다: {target}")

    source.rename(target)
    log(f"변경: {source} -> {target.name}")
    return True


def change_extensions(folder: Path, recursive: bool, log) -> RenameStats:
    stats = RenameStats()
    for file_path in iter_files(folder, recursive, log):
        stats.scanned += 1
        target, reason = extension_change_target(file_path)
        if target is None:
            stats.skipped += 1
            log(f"건너뜀: {file_path} ({reason})")
            continue

        try:
            if rename_without_overwrite(file_path, target, log):
                stats.changed += 1
            else:
                stats.skipped += 1
        except OSError as exc:
            stats.failed += 1
            stats.error_files.append(file_path.name)
            log(f"실패: {file_path} ({exc})")

    return stats


def resolve_bandizip_executable(bandizip: Path) -> Path:
    if bandizip.name.lower() == "bandizip.exe":
        console_tool = bandizip.with_name("bz.exe")
        if console_tool.is_file():
            return console_tool
    return bandizip


def bandizip_command(bandizip: Path, archive: Path, output_dir: Path, password: str):
    command = [str(resolve_bandizip_executable(bandizip)), "x", "-y", f"-o:{output_dir}"]
    if password:
        command.append(f"-p:{password}")
    command.append(str(archive))
    return command


def run_bandizip(bandizip: Path, archive: Path, output_dir: Path, password: str):
    output_dir.mkdir(parents=True, exist_ok=True)
    encoding = locale.getpreferredencoding(False)
    completed = subprocess.run(
        bandizip_command(bandizip, archive, output_dir, password),
        capture_output=True,
        text=True,
        encoding=encoding,
        errors="replace",
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or "").strip()
        if detail:
            raise RuntimeError(detail[-1000:])
        raise RuntimeError(f"Bandizip 종료 코드 {completed.returncode}")


def find_first_pass_archives(folder: Path, recursive: bool, log):
    archives = []
    for file_path in iter_files(folder, recursive, log):
        if file_path.suffix.lower() in ARCHIVE_EXTENSIONS:
            archives.append(file_path)
    return filter_part_rar_archives(archives, log)


def part_rar_number(path: Path):
    if path.suffix.lower() != ".rar":
        return None
    match = PART_RAR_RE.search(path.stem)
    if match is None:
        return None
    return int(match.group("number"))


def filter_part_rar_archives(archives, log):
    filtered = []
    for archive in archives:
        number = part_rar_number(archive)
        if number is not None and number != 1:
            log(f"건너뜀: 분할 rar 후속 파일 - {archive}")
            continue
        filtered.append(archive)
    return filtered


def pick_largest_second_pass_archive(directory: Path):
    candidates = []
    try:
        children = sorted(directory.iterdir(), key=lambda item: item.name.lower())
    except OSError:
        return None

    for child in children:
        if child.is_file() and child.suffix.lower() in SECOND_PASS_EXTENSIONS:
            candidates.append(child)

    if not candidates:
        return None
    return max(candidates, key=lambda item: item.stat().st_size)


def prepare_second_pass_archive(archive: Path, log):
    if archive.suffix.lower() != ".zi":
        return archive

    target = archive.with_suffix(".zip")
    if normalize_key(archive) == normalize_key(target):
        return target
    if target.exists():
        raise FileExistsError(f"변경할 대상 파일이 이미 있습니다: {target}")
    archive.rename(target)
    log(f"zi 이름 변경: {archive} -> {target.name}")
    return target


def extract_archives(folder: Path, recursive: bool, bandizip: Path, password: str, log) -> ExtractStats:
    stats = ExtractStats()
    archives = find_first_pass_archives(folder, recursive, log)
    stats.first_targets = len(archives)
    log(f"1차 압축 대상: {stats.first_targets}개")

    extracted_roots = []
    for archive in archives:
        output_dir = archive.with_suffix("")
        try:
            log(f"1차 압축풀기: {archive}")
            run_bandizip(bandizip, archive, output_dir, password)
            extracted_roots.append(output_dir)
            stats.first_success += 1
        except Exception as exc:
            stats.first_failed += 1
            stats.error_files.append(archive.name)
            log(f"1차 실패: {archive} ({exc})")

    log("1차 압축풀기 작업이 끝났습니다. 2차 압축파일을 검색합니다.")

    visited_dirs = set()
    second_archives = []
    for root in extracted_roots:
        if not root.exists():
            log(f"건너뜀: 압축풀기 폴더가 없습니다 - {root}")
            continue
        for directory in iter_directories(root, log):
            directory_key = normalize_key(directory)
            if directory_key in visited_dirs:
                continue
            visited_dirs.add(directory_key)
            picked = pick_largest_second_pass_archive(directory)
            if picked is not None:
                second_archives.append(picked)

    stats.second_targets = len(second_archives)
    log(f"2차 압축 대상: {stats.second_targets}개")

    processed = set()
    for archive in second_archives:
        try:
            prepared = prepare_second_pass_archive(archive, log)
            prepared_key = normalize_key(prepared)
            if prepared_key in processed:
                continue
            processed.add(prepared_key)

            log(f"2차 압축풀기: {prepared}")
            run_bandizip(bandizip, prepared, prepared.parent, password)
            stats.second_success += 1
        except Exception as exc:
            stats.second_failed += 1
            stats.error_files.append(archive.name)
            log(f"2차 실패: {archive} ({exc})")

    return stats


def log_error_file_names(error_files, log):
    if not error_files:
        return
    log("오류 발생 파일명:")
    for file_name in error_files:
        log(file_name)


class MaengchamHelper(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(APP_NAME)
        self.geometry("860x620")
        self.minsize(760, 520)

        self.log_queue = queue.Queue()
        self.buttons = {}

        self.ext_folder_var = tk.StringVar(value=DEFAULT_DOWNLOAD_DIR)
        self.ext_recursive_var = tk.BooleanVar(value=True)
        self.extract_folder_var = tk.StringVar(value=DEFAULT_DOWNLOAD_DIR)
        self.extract_recursive_var = tk.BooleanVar(value=True)
        self.bandizip_var = tk.StringVar(value=DEFAULT_BANDIZIP)
        self.password_var = tk.StringVar(value=DEFAULT_PASSWORD)
        self.status_var = tk.StringVar(value="대기 중")

        self._build_style()
        self._build_ui()
        self.after(100, self._drain_log_queue)

    def _build_style(self):
        style = ttk.Style(self)
        try:
            style.theme_use("vista")
        except tk.TclError:
            pass
        style.configure("TButton", padding=(10, 6))
        style.configure("Primary.TButton", padding=(12, 7))
        style.configure("TEntry", padding=(6, 4))

    def _build_ui(self):
        container = ttk.Frame(self, padding=12)
        container.pack(fill=tk.BOTH, expand=True)

        title_row = ttk.Frame(container)
        title_row.pack(fill=tk.X)
        ttk.Label(title_row, text=APP_NAME, font=("맑은 고딕", 16, "bold")).pack(side=tk.LEFT)
        ttk.Label(title_row, textvariable=self.status_var).pack(side=tk.RIGHT)

        notebook = ttk.Notebook(container)
        notebook.pack(fill=tk.BOTH, expand=True, pady=(12, 8))

        extension_tab = ttk.Frame(notebook, padding=12)
        extract_tab = ttk.Frame(notebook, padding=12)
        notebook.add(extension_tab, text="파일 확장자 변경")
        notebook.add(extract_tab, text="다중 압축 풀기")

        self._build_extension_tab(extension_tab)
        self._build_extract_tab(extract_tab)
        self._build_log(container)

    def _build_extension_tab(self, parent):
        parent.columnconfigure(1, weight=1)

        ttk.Label(parent, text="대상 폴더").grid(row=0, column=0, sticky=tk.W, pady=6)
        ttk.Entry(parent, textvariable=self.ext_folder_var).grid(row=0, column=1, sticky=tk.EW, padx=8)
        ttk.Button(parent, text="찾기", command=lambda: self._pick_folder(self.ext_folder_var)).grid(
            row=0, column=2, sticky=tk.E
        )

        ttk.Checkbutton(parent, text="하위폴더 포함", variable=self.ext_recursive_var).grid(
            row=1, column=1, sticky=tk.W, pady=6
        )

        button = ttk.Button(parent, text="확장자 변경 시작", style="Primary.TButton", command=self.start_extension_change)
        button.grid(row=2, column=1, sticky=tk.W, pady=(12, 0))
        self.buttons["extension"] = button

    def _build_extract_tab(self, parent):
        parent.columnconfigure(1, weight=1)

        ttk.Label(parent, text="압축파일 폴더").grid(row=0, column=0, sticky=tk.W, pady=6)
        ttk.Entry(parent, textvariable=self.extract_folder_var).grid(row=0, column=1, sticky=tk.EW, padx=8)
        ttk.Button(parent, text="찾기", command=lambda: self._pick_folder(self.extract_folder_var)).grid(
            row=0, column=2, sticky=tk.E
        )

        ttk.Checkbutton(parent, text="하위폴더 포함", variable=self.extract_recursive_var).grid(
            row=1, column=1, sticky=tk.W, pady=6
        )

        ttk.Label(parent, text="반디집 실행 파일").grid(row=2, column=0, sticky=tk.W, pady=6)
        ttk.Entry(parent, textvariable=self.bandizip_var).grid(row=2, column=1, sticky=tk.EW, padx=8)
        ttk.Button(parent, text="찾기", command=self._pick_bandizip).grid(row=2, column=2, sticky=tk.E)

        ttk.Label(parent, text="비밀번호").grid(row=3, column=0, sticky=tk.W, pady=6)
        ttk.Entry(parent, textvariable=self.password_var).grid(row=3, column=1, sticky=tk.EW, padx=8)

        button = ttk.Button(parent, text="압축 풀기 시작", style="Primary.TButton", command=self.start_extract)
        button.grid(row=4, column=1, sticky=tk.W, pady=(12, 0))
        self.buttons["extract"] = button

    def _build_log(self, parent):
        log_frame = ttk.LabelFrame(parent, text="작업 로그", padding=8)
        log_frame.pack(fill=tk.BOTH, expand=True)
        log_frame.rowconfigure(0, weight=1)
        log_frame.columnconfigure(0, weight=1)

        self.log_text = tk.Text(log_frame, height=13, wrap=tk.WORD, state=tk.DISABLED)
        self.log_text.grid(row=0, column=0, sticky=tk.NSEW)
        scrollbar = ttk.Scrollbar(log_frame, command=self.log_text.yview)
        scrollbar.grid(row=0, column=1, sticky=tk.NS)
        self.log_text.configure(yscrollcommand=scrollbar.set)

        ttk.Button(log_frame, text="로그 지우기", command=self.clear_log).grid(row=1, column=0, sticky=tk.E, pady=(8, 0))

    def _pick_folder(self, variable):
        selected = filedialog.askdirectory(initialdir=variable.get() or DEFAULT_DOWNLOAD_DIR)
        if selected:
            variable.set(selected)

    def _pick_bandizip(self):
        selected = filedialog.askopenfilename(
            initialdir=str(Path(self.bandizip_var.get()).parent),
            filetypes=[("실행 파일", "*.exe"), ("모든 파일", "*.*")],
        )
        if selected:
            self.bandizip_var.set(selected)

    def clear_log(self):
        self.log_text.configure(state=tk.NORMAL)
        self.log_text.delete("1.0", tk.END)
        self.log_text.configure(state=tk.DISABLED)

    def log(self, message: str):
        self.log_queue.put(("log", message))

    def _append_log(self, message: str):
        self.log_text.configure(state=tk.NORMAL)
        self.log_text.insert(tk.END, message + "\n")
        self.log_text.see(tk.END)
        self.log_text.configure(state=tk.DISABLED)

    def _drain_log_queue(self):
        try:
            while True:
                kind, payload = self.log_queue.get_nowait()
                if kind == "log":
                    self._append_log(payload)
                elif kind == "done":
                    task_name, ok = payload
                    self.buttons[task_name].configure(state=tk.NORMAL)
                    self.status_var.set("완료" if ok else "오류")
        except queue.Empty:
            pass
        self.after(100, self._drain_log_queue)

    def _validate_folder(self, value: str, label: str):
        path = Path(value.strip().strip('"'))
        if not path.exists() or not path.is_dir():
            messagebox.showerror(APP_NAME, f"{label}를 확인할 수 없습니다.\n{path}")
            return None
        return path

    def _validate_file(self, value: str, label: str):
        path = Path(value.strip().strip('"'))
        if not path.exists() or not path.is_file():
            messagebox.showerror(APP_NAME, f"{label}을 확인할 수 없습니다.\n{path}")
            return None
        return path

    def _start_worker(self, task_name: str, worker):
        self.buttons[task_name].configure(state=tk.DISABLED)
        self.status_var.set("작업 중")

        def wrapped():
            ok = True
            try:
                worker()
            except Exception:
                ok = False
                self.log("예상하지 못한 오류가 발생했습니다.")
                self.log(traceback.format_exc())
            finally:
                self.log_queue.put(("done", (task_name, ok)))

        threading.Thread(target=wrapped, daemon=True).start()

    def start_extension_change(self):
        folder = self._validate_folder(self.ext_folder_var.get(), "대상 폴더")
        if folder is None:
            return
        recursive = self.ext_recursive_var.get()

        def worker():
            self.log(f"확장자 변경 시작: {folder}")
            stats = change_extensions(folder, recursive, self.log)
            self.log(
                "확장자 변경 완료: "
                f"검색 {stats.scanned}개, 변경 {stats.changed}개, "
                f"건너뜀 {stats.skipped}개, 실패 {stats.failed}개"
            )
            log_error_file_names(stats.error_files, self.log)

        self._start_worker("extension", worker)

    def start_extract(self):
        folder = self._validate_folder(self.extract_folder_var.get(), "압축파일 폴더")
        if folder is None:
            return
        bandizip = self._validate_file(self.bandizip_var.get(), "반디집 실행 파일")
        if bandizip is None:
            return
        recursive = self.extract_recursive_var.get()
        password = self.password_var.get()

        def worker():
            self.log(f"다중 압축 풀기 시작: {folder}")
            stats = extract_archives(folder, recursive, bandizip, password, self.log)
            self.log(
                "다중 압축 풀기 완료: "
                f"1차 대상 {stats.first_targets}개, 1차 성공 {stats.first_success}개, "
                f"1차 실패 {stats.first_failed}개, 2차 대상 {stats.second_targets}개, "
                f"2차 성공 {stats.second_success}개, 2차 실패 {stats.second_failed}개"
            )
            log_error_file_names(stats.error_files, self.log)

        self._start_worker("extract", worker)


def main():
    if sys.platform.startswith("win"):
        try:
            import ctypes

            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("baidu.renamer")
        except Exception:
            pass

    app = MaengchamHelper()
    app.mainloop()__name__ == "__main__":
    main()
