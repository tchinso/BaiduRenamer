using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace BaiduRenamer
{
    internal static class Program
    {
        [STAThread]
        private static void Main(string[] args)
        {
            if (args != null && args.Length == 1 && String.Equals(args[0], "--self-test", StringComparison.OrdinalIgnoreCase))
            {
                Environment.Exit(SelfTest.Run());
                return;
            }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }

    internal sealed class MainForm : Form
    {
        private const string DefaultDownloadDir = @"J:\BaiduNetdiskDownload";
        private const string DefaultBandizip = @"C:\Program Files\Bandizip\Bandizip.exe";
        private const string DefaultPassword = "himengzhan.vip";

        private readonly TabControl tabs;
        private readonly TextBox extensionFolder;
        private readonly CheckBox extensionRecursive;
        private readonly Button extensionStart;
        private readonly Button undoSelected;

        private readonly TextBox extractFolder;
        private readonly CheckBox extractRecursive;
        private readonly TextBox bandizipPath;
        private readonly TextBox password;
        private readonly Button extractStart;

        private readonly DataGridView logGrid;
        private readonly ToolStripStatusLabel statusLabel;

        private volatile bool busy;

        public MainForm()
        {
            Text = "BaiduRenamer C#";
            StartPosition = FormStartPosition.CenterScreen;
            MinimumSize = new Size(980, 620);
            Size = new Size(1120, 720);
            AutoScaleMode = AutoScaleMode.Dpi;

            tabs = new TabControl();
            tabs.Dock = DockStyle.Top;
            tabs.Height = 242;

            TabPage extensionPage = new TabPage("파일 확장자 변경");
            TabPage extractPage = new TabPage("다중 압축 풀기");
            tabs.TabPages.Add(extensionPage);
            tabs.TabPages.Add(extractPage);

            extensionFolder = new TextBox();
            extensionFolder.Text = DefaultDownloadDir;
            extensionFolder.Dock = DockStyle.Fill;
            Button extensionBrowse = new Button();
            extensionBrowse.Text = "찾기";
            extensionBrowse.Dock = DockStyle.Fill;
            extensionBrowse.Click += delegate { BrowseFolder(extensionFolder); };
            extensionRecursive = new CheckBox();
            extensionRecursive.Text = "하위폴더 포함";
            extensionRecursive.Checked = true;
            extensionStart = new Button();
            extensionStart.Text = "확장자 변경 시작";
            extensionStart.Click += delegate { StartExtensionTask(); };
            undoSelected = new Button();
            undoSelected.Text = "체크한 확장자 변환 되돌리기";
            undoSelected.Click += delegate { UndoCheckedExtensionChanges(); };
            extensionPage.Controls.Add(BuildExtensionPanel(extensionBrowse));

            extractFolder = new TextBox();
            extractFolder.Text = DefaultDownloadDir;
            extractFolder.Dock = DockStyle.Fill;
            Button extractBrowse = new Button();
            extractBrowse.Text = "찾기";
            extractBrowse.Dock = DockStyle.Fill;
            extractBrowse.Click += delegate { BrowseFolder(extractFolder); };
            extractRecursive = new CheckBox();
            extractRecursive.Text = "하위폴더 포함";
            extractRecursive.Checked = true;
            bandizipPath = new TextBox();
            bandizipPath.Text = DefaultBandizip;
            bandizipPath.Dock = DockStyle.Fill;
            Button bandizipBrowse = new Button();
            bandizipBrowse.Text = "찾기";
            bandizipBrowse.Dock = DockStyle.Fill;
            bandizipBrowse.Click += delegate { BrowseBandizip(); };
            password = new TextBox();
            password.Text = DefaultPassword;
            password.Dock = DockStyle.Fill;
            extractStart = new Button();
            extractStart.Text = "압축 풀기 시작";
            extractStart.Click += delegate { StartExtractTask(); };
            extractPage.Controls.Add(BuildExtractPanel(extractBrowse, bandizipBrowse));

            logGrid = new DataGridView();
            logGrid.Dock = DockStyle.Fill;
            logGrid.AllowUserToAddRows = false;
            logGrid.AllowUserToDeleteRows = false;
            logGrid.AllowUserToResizeRows = false;
            logGrid.MultiSelect = true;
            logGrid.RowHeadersVisible = false;
            logGrid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
            logGrid.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill;
            logGrid.BackgroundColor = SystemColors.Window;
            logGrid.BorderStyle = BorderStyle.Fixed3D;
            logGrid.CurrentCellDirtyStateChanged += delegate
            {
                if (logGrid.IsCurrentCellDirty)
                {
                    logGrid.CommitEdit(DataGridViewDataErrorContexts.Commit);
                }
            };
            BuildLogColumns();

            StatusStrip statusStrip = new StatusStrip();
            statusLabel = new ToolStripStatusLabel();
            statusLabel.Text = "대기 중";
            statusStrip.Items.Add(statusLabel);

            Panel logTools = new Panel();
            logTools.Dock = DockStyle.Top;
            logTools.Height = 38;
            Button clearLog = new Button();
            clearLog.Text = "로그 지우기";
            clearLog.Width = 110;
            clearLog.Height = 28;
            clearLog.Left = 8;
            clearLog.Top = 5;
            clearLog.Click += delegate { logGrid.Rows.Clear(); };
            logTools.Controls.Add(clearLog);

            Controls.Add(logGrid);
            Controls.Add(logTools);
            Controls.Add(tabs);
            Controls.Add(statusStrip);
        }

        private Control BuildExtensionPanel(Button browse)
        {
            TableLayoutPanel panel = new TableLayoutPanel();
            panel.Dock = DockStyle.Fill;
            panel.Padding = new Padding(12);
            panel.ColumnCount = 3;
            panel.RowCount = 3;
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 92));
            panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
            panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 38));
            panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));

            Label folderLabel = MakeLabel("대상 폴더");
            panel.Controls.Add(folderLabel, 0, 0);
            panel.Controls.Add(extensionFolder, 1, 0);
            panel.Controls.Add(browse, 2, 0);
            panel.Controls.Add(extensionRecursive, 1, 1);

            FlowLayoutPanel actions = new FlowLayoutPanel();
            actions.Dock = DockStyle.Fill;
            actions.FlowDirection = FlowDirection.LeftToRight;
            actions.Controls.Add(extensionStart);
            actions.Controls.Add(undoSelected);
            extensionStart.Width = 150;
            undoSelected.Width = 220;
            panel.Controls.Add(actions, 1, 2);
            panel.SetColumnSpan(actions, 2);
            return panel;
        }

        private Control BuildExtractPanel(Button folderBrowse, Button bandizipBrowse)
        {
            TableLayoutPanel panel = new TableLayoutPanel();
            panel.Dock = DockStyle.Fill;
            panel.Padding = new Padding(12);
            panel.ColumnCount = 3;
            panel.RowCount = 5;
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120));
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 92));
            panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
            panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
            panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
            panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
            panel.RowStyles.Add(new RowStyle(SizeType.Absolute, 42));

            panel.Controls.Add(MakeLabel("압축파일 폴더"), 0, 0);
            panel.Controls.Add(extractFolder, 1, 0);
            panel.Controls.Add(folderBrowse, 2, 0);
            panel.Controls.Add(extractRecursive, 1, 1);
            panel.Controls.Add(MakeLabel("Bandizip.exe"), 0, 2);
            panel.Controls.Add(bandizipPath, 1, 2);
            panel.Controls.Add(bandizipBrowse, 2, 2);
            panel.Controls.Add(MakeLabel("암호"), 0, 3);
            panel.Controls.Add(password, 1, 3);

            FlowLayoutPanel actions = new FlowLayoutPanel();
            actions.Dock = DockStyle.Fill;
            actions.FlowDirection = FlowDirection.LeftToRight;
            extractStart.Width = 150;
            actions.Controls.Add(extractStart);
            panel.Controls.Add(actions, 1, 4);
            panel.SetColumnSpan(actions, 2);
            return panel;
        }

        private static Label MakeLabel(string text)
        {
            Label label = new Label();
            label.Text = text;
            label.TextAlign = ContentAlignment.MiddleLeft;
            label.Dock = DockStyle.Fill;
            return label;
        }

        private void BuildLogColumns()
        {
            DataGridViewCheckBoxColumn undoColumn = new DataGridViewCheckBoxColumn();
            undoColumn.HeaderText = "되돌리기";
            undoColumn.FillWeight = 45;
            undoColumn.MinimumWidth = 70;
            logGrid.Columns.Add(undoColumn);

            DataGridViewTextBoxColumn taskColumn = new DataGridViewTextBoxColumn();
            taskColumn.HeaderText = "작업명";
            taskColumn.FillWeight = 82;
            taskColumn.ReadOnly = true;
            logGrid.Columns.Add(taskColumn);

            DataGridViewTextBoxColumn sourceColumn = new DataGridViewTextBoxColumn();
            sourceColumn.HeaderText = "원본파일명";
            sourceColumn.FillWeight = 170;
            sourceColumn.ReadOnly = true;
            logGrid.Columns.Add(sourceColumn);

            DataGridViewTextBoxColumn resultColumn = new DataGridViewTextBoxColumn();
            resultColumn.HeaderText = "결과 경로";
            resultColumn.FillWeight = 170;
            resultColumn.ReadOnly = true;
            logGrid.Columns.Add(resultColumn);

            DataGridViewTextBoxColumn statusColumn = new DataGridViewTextBoxColumn();
            statusColumn.HeaderText = "성공 여부";
            statusColumn.FillWeight = 118;
            statusColumn.ReadOnly = true;
            logGrid.Columns.Add(statusColumn);
        }

        private void BrowseFolder(TextBox target)
        {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                string current = CleanInput(target.Text);
                if (Directory.Exists(current))
                {
                    dialog.SelectedPath = current;
                }
                if (dialog.ShowDialog(this) == DialogResult.OK)
                {
                    target.Text = dialog.SelectedPath;
                }
            }
        }

        private void BrowseBandizip()
        {
            using (OpenFileDialog dialog = new OpenFileDialog())
            {
                dialog.Filter = "Bandizip.exe|Bandizip.exe|실행 파일|*.exe|모든 파일|*.*";
                string current = CleanInput(bandizipPath.Text);
                if (File.Exists(current))
                {
                    dialog.InitialDirectory = Path.GetDirectoryName(current);
                    dialog.FileName = Path.GetFileName(current);
                }
                if (dialog.ShowDialog(this) == DialogResult.OK)
                {
                    bandizipPath.Text = dialog.FileName;
                }
            }
        }

        private void StartExtensionTask()
        {
            string folder;
            if (!TryGetFolder(extensionFolder.Text, "대상 폴더", out folder))
            {
                return;
            }

            bool recursive = extensionRecursive.Checked;
            RunBackground("확장자 변환", delegate
            {
                ExtensionProcessor.ChangeExtensions(folder, recursive, AddLog);
            });
        }

        private void StartExtractTask()
        {
            string folder;
            if (!TryGetFolder(extractFolder.Text, "압축파일 폴더", out folder))
            {
                return;
            }

            string exe = CleanInput(bandizipPath.Text);
            if (!File.Exists(exe))
            {
                MessageBox.Show(this, "Bandizip.exe 파일을 찾을 수 없습니다.", "경로 확인", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
            if (!String.Equals(Path.GetFileName(exe), "Bandizip.exe", StringComparison.OrdinalIgnoreCase))
            {
                MessageBox.Show(this, "진행도 표시를 위해 Bandizip.exe를 선택해야 합니다. bz.exe는 사용하지 않습니다.", "Bandizip.exe 필요", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            bool recursive = extractRecursive.Checked;
            string pass = password.Text;
            RunBackground("압축 풀기", delegate
            {
                ArchiveProcessor.ExtractArchives(folder, recursive, exe, pass, AddLog);
            });
        }

        private void UndoCheckedExtensionChanges()
        {
            List<UndoRequest> requests = new List<UndoRequest>();
            foreach (DataGridViewRow row in logGrid.Rows)
            {
                LogEntry entry = row.Tag as LogEntry;
                if (entry == null || !entry.Undoable)
                {
                    continue;
                }

                object value = row.Cells[0].Value;
                bool selected = value is bool && (bool)value;
                if (selected)
                {
                    requests.Add(new UndoRequest(row.Index, entry));
                }
            }

            if (requests.Count == 0)
            {
                MessageBox.Show(this, "되돌릴 확장자 변환 로그를 체크하세요.", "선택 필요", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            RunBackground("확장자 변환 되돌리기", delegate
            {
                foreach (UndoRequest request in requests)
                {
                    UndoOne(request);
                }
            });
        }

        private void UndoOne(UndoRequest request)
        {
            LogEntry entry = request.Entry;
            try
            {
                if (!File.Exists(entry.UndoFrom))
                {
                    AddLog(LogEntry.Failure("확장자변환 되돌리기", entry.UndoFrom, entry.UndoTo, "실패: 변경된 파일이 없습니다."));
                    return;
                }
                if (File.Exists(entry.UndoTo) || Directory.Exists(entry.UndoTo))
                {
                    AddLog(LogEntry.Failure("확장자변환 되돌리기", entry.UndoFrom, entry.UndoTo, "실패: 원래 경로가 이미 있습니다."));
                    return;
                }

                File.Move(entry.UndoFrom, entry.UndoTo);
                AddLog(LogEntry.Success("확장자변환 되돌리기", entry.UndoFrom, entry.UndoTo));
                MarkUndoConsumed(request.RowIndex);
            }
            catch (Exception ex)
            {
                AddLog(LogEntry.Failure("확장자변환 되돌리기", entry.UndoFrom, entry.UndoTo, "실패: " + ex.Message));
            }
        }

        private void MarkUndoConsumed(int rowIndex)
        {
            if (IsDisposed)
            {
                return;
            }
            if (InvokeRequired)
            {
                BeginInvoke((MethodInvoker)delegate { MarkUndoConsumed(rowIndex); });
                return;
            }

            if (rowIndex < 0 || rowIndex >= logGrid.Rows.Count)
            {
                return;
            }
            DataGridViewRow row = logGrid.Rows[rowIndex];
            LogEntry entry = row.Tag as LogEntry;
            if (entry != null)
            {
                entry.Undoable = false;
            }
            row.Cells[0].Value = false;
            row.Cells[0].ReadOnly = true;
            row.Cells[0].Style.BackColor = Color.Gainsboro;
        }

        private bool TryGetFolder(string text, string label, out string folder)
        {
            folder = CleanInput(text);
            if (folder.Length == 0)
            {
                MessageBox.Show(this, label + "를 입력하세요.", "경로 확인", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            if (!Directory.Exists(folder))
            {
                MessageBox.Show(this, label + "를 찾을 수 없습니다.", "경로 확인", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return false;
            }
            folder = Path.GetFullPath(folder);
            return true;
        }

        private static string CleanInput(string value)
        {
            if (value == null)
            {
                return String.Empty;
            }
            string trimmed = value.Trim();
            while (trimmed.Length > 1 && trimmed[0] == '"' && trimmed[trimmed.Length - 1] == '"')
            {
                trimmed = trimmed.Substring(1, trimmed.Length - 2).Trim();
            }
            return trimmed;
        }

        private void RunBackground(string name, Action action)
        {
            if (busy)
            {
                return;
            }

            SetBusy(true);
            statusLabel.Text = name + " 작업 중";
            Task.Factory.StartNew(delegate
            {
                Exception error = null;
                try
                {
                    action();
                }
                catch (Exception ex)
                {
                    error = ex;
                    AddLog(LogEntry.Failure(name, "", "", "실패: " + ex.Message));
                }
                return error;
            }).ContinueWith(delegate(Task<Exception> task)
            {
                if (IsDisposed)
                {
                    return;
                }
                BeginInvoke((MethodInvoker)delegate
                {
                    SetBusy(false);
                    statusLabel.Text = task.Result == null ? "완료" : "오류";
                });
            });
        }

        private void SetBusy(bool value)
        {
            busy = value;
            extensionStart.Enabled = !value;
            extractStart.Enabled = !value;
            undoSelected.Enabled = !value;
        }

        private void AddLog(LogEntry entry)
        {
            if (IsDisposed)
            {
                return;
            }
            if (InvokeRequired)
            {
                BeginInvoke((MethodInvoker)delegate { AddLog(entry); });
                return;
            }

            int index = logGrid.Rows.Add(false, entry.TaskName, entry.Source, entry.Result, entry.StatusText);
            DataGridViewRow row = logGrid.Rows[index];
            row.Tag = entry;
            row.Cells[0].ReadOnly = !entry.Undoable;
            if (!entry.Undoable)
            {
                row.Cells[0].Style.BackColor = Color.Gainsboro;
            }
            if (entry.IsFailure)
            {
                row.DefaultCellStyle.BackColor = Color.MistyRose;
                row.DefaultCellStyle.ForeColor = Color.DarkRed;
                row.DefaultCellStyle.SelectionBackColor = Color.IndianRed;
                row.DefaultCellStyle.SelectionForeColor = Color.White;
            }
            else if (entry.StatusText.StartsWith("건너뜀", StringComparison.Ordinal))
            {
                row.DefaultCellStyle.BackColor = Color.FromArgb(245, 245, 245);
            }

            if (logGrid.Rows.Count > 0)
            {
                logGrid.FirstDisplayedScrollingRowIndex = logGrid.Rows.Count - 1;
            }
        }

        private sealed class UndoRequest
        {
            public readonly int RowIndex;
            public readonly LogEntry Entry;

            public UndoRequest(int rowIndex, LogEntry entry)
            {
                RowIndex = rowIndex;
                Entry = entry;
            }
        }
    }

    internal sealed class LogEntry
    {
        public string TaskName;
        public string Source;
        public string Result;
        public string StatusText;
        public bool IsFailure;
        public bool Undoable;
        public string UndoFrom;
        public string UndoTo;

        public static LogEntry Success(string taskName, string source, string result)
        {
            return new LogEntry
            {
                TaskName = taskName,
                Source = source,
                Result = result,
                StatusText = "성공",
                IsFailure = false
            };
        }

        public static LogEntry Skipped(string taskName, string source, string result, string reason)
        {
            return new LogEntry
            {
                TaskName = taskName,
                Source = source,
                Result = result,
                StatusText = "건너뜀: " + reason,
                IsFailure = false
            };
        }

        public static LogEntry Failure(string taskName, string source, string result, string status)
        {
            return new LogEntry
            {
                TaskName = taskName,
                Source = source,
                Result = result,
                StatusText = status,
                IsFailure = true
            };
        }

        public static LogEntry UndoableSuccess(string taskName, string source, string result)
        {
            LogEntry entry = Success(taskName, source, result);
            entry.Undoable = true;
            entry.UndoFrom = result;
            entry.UndoTo = source;
            return entry;
        }
    }

    internal static class ExtensionProcessor
    {
        public static void ChangeExtensions(string folder, bool recursive, Action<LogEntry> log)
        {
            List<string> files = FileSystem.CollectFiles(folder, recursive, log);
            foreach (string file in files)
            {
                ExtensionDecision decision = ExtensionRules.Decide(file);
                if (!decision.ShouldChange)
                {
                    log(LogEntry.Skipped("확장자변환", file, "", decision.Reason));
                    continue;
                }

                try
                {
                    if (PathRules.SamePath(file, decision.Target))
                    {
                        log(LogEntry.Skipped("확장자변환", file, decision.Target, "이미 대상 이름입니다."));
                        continue;
                    }
                    if (File.Exists(decision.Target) || Directory.Exists(decision.Target))
                    {
                        log(LogEntry.Failure("확장자변환", file, decision.Target, "실패: 대상 파일이 이미 있습니다."));
                        continue;
                    }

                    File.Move(file, decision.Target);
                    log(LogEntry.UndoableSuccess("확장자변환", file, decision.Target));
                }
                catch (Exception ex)
                {
                    log(LogEntry.Failure("확장자변환", file, decision.Target, "실패: " + ex.Message));
                }
            }
        }
    }

    internal sealed class ExtensionDecision
    {
        public bool ShouldChange;
        public string Target;
        public string Reason;

        public static ExtensionDecision Change(string target, string reason)
        {
            return new ExtensionDecision { ShouldChange = true, Target = target, Reason = reason };
        }

        public static ExtensionDecision Skip(string reason)
        {
            return new ExtensionDecision { ShouldChange = false, Target = "", Reason = reason };
        }
    }

    internal static class ExtensionRules
    {
        public static ExtensionDecision Decide(string path)
        {
            if (HasExeSuffix(path))
            {
                return ExtensionDecision.Skip("exe 파일");
            }

            if (IsExactDateMp4Name(path))
            {
                return ExtensionDecision.Change(PathRules.WithSuffix(path, ".zip"), "YY.MM.mp4/YYYY.MM.mp4 상위 규칙");
            }

            int numberedPart;
            if (IsSevenZipNumberedPart(path, out numberedPart))
            {
                return ExtensionDecision.Skip("7z 분할 파일 보존");
            }

            string splitTarget;
            if (TrySevenZipDisguisedFirstPart(path, out splitTarget))
            {
                return ExtensionDecision.Change(splitTarget, "7z 분할 첫 파일을 .001로 보정");
            }

            char frontFirst;
            if (PathRules.TryGetFrontSuffixFirstChar(path, out frontFirst))
            {
                if (frontFirst == '7' || frontFirst == 'r')
                {
                    return ExtensionDecision.Change(PathRules.RemoveSuffix(path), "이중확장자 뒤 확장자 제거");
                }
                if (frontFirst == '0')
                {
                    return ExtensionDecision.Skip("0으로 시작하는 이중확장자");
                }
            }

            if (!PathRules.HasSuffix(path))
            {
                return ExtensionDecision.Change(path + ".zip", "zip 확장자 추가");
            }

            char lastFirst;
            if (PathRules.TryGetLastSuffixFirstChar(path, out lastFirst))
            {
                if (lastFirst == '0')
                {
                    return ExtensionDecision.Skip("0으로 시작하는 확장자");
                }
                if (lastFirst == '7')
                {
                    return ExtensionDecision.Change(PathRules.WithSuffix(path, ".7z"), "7z로 변경");
                }
                if (lastFirst == 'r')
                {
                    return ExtensionDecision.Change(PathRules.WithSuffix(path, ".rar"), "rar로 변경");
                }
            }

            return ExtensionDecision.Change(PathRules.WithSuffix(path, ".zip"), "zip으로 변경");
        }

        private static bool HasExeSuffix(string path)
        {
            string name = Path.GetFileName(path);
            if (String.IsNullOrEmpty(name))
            {
                return false;
            }

            for (int i = 0; i < name.Length; i++)
            {
                if (name[i] != '.' || i == 0 || i + 1 >= name.Length)
                {
                    continue;
                }

                int start = i + 1;
                int end = start;
                while (end < name.Length && name[end] != '.')
                {
                    end++;
                }

                if (end - start == 3 && String.Compare(name, start, "exe", 0, 3, true, CultureInfo.InvariantCulture) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        private static bool IsExactDateMp4Name(string path)
        {
            string name = Path.GetFileName(path);
            if (name == null || (name.Length != 9 && name.Length != 11))
            {
                return false;
            }
            if (!name.EndsWith(".mp4", StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }

            int stemLength = name.Length - 4;
            if (stemLength == 5)
            {
                return Char.IsDigit(name[0]) && Char.IsDigit(name[1]) &&
                    name[2] == '.' && ParseMonth(name, 3);
            }

            return Char.IsDigit(name[0]) && Char.IsDigit(name[1]) &&
                Char.IsDigit(name[2]) && Char.IsDigit(name[3]) &&
                name[4] == '.' && ParseMonth(name, 5);
        }

        private static bool ParseMonth(string text, int start)
        {
            if (start + 1 >= text.Length || !Char.IsDigit(text[start]) || !Char.IsDigit(text[start + 1]))
            {
                return false;
            }
            int month = (text[start] - '0') * 10 + (text[start + 1] - '0');
            return month >= 1 && month <= 12;
        }

        private static bool IsSevenZipNumberedPart(string path, out int number)
        {
            number = -1;
            string suffix = PathRules.GetSuffix(path);
            if (!IsThreeDigitSuffix(suffix, out number))
            {
                return false;
            }

            string withoutNumber = PathRules.RemoveSuffix(path);
            return String.Equals(PathRules.GetSuffix(withoutNumber), ".7z", StringComparison.OrdinalIgnoreCase);
        }

        private static bool TrySevenZipDisguisedFirstPart(string path, out string target)
        {
            target = null;
            string withoutLast = PathRules.RemoveSuffix(path);
            if (String.Equals(withoutLast, path, StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }
            if (!String.Equals(PathRules.GetSuffix(withoutLast), ".7z", StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }
            if (!HasFollowingSevenZipPart(withoutLast))
            {
                return false;
            }

            target = withoutLast + ".001";
            return true;
        }

        private static bool HasFollowingSevenZipPart(string basePath)
        {
            string directory = Path.GetDirectoryName(basePath);
            if (String.IsNullOrEmpty(directory))
            {
                directory = Directory.GetCurrentDirectory();
            }

            string prefix = Path.GetFileName(basePath) + ".";
            try
            {
                string[] files = Directory.GetFiles(directory);
                for (int i = 0; i < files.Length; i++)
                {
                    string name = Path.GetFileName(files[i]);
                    if (name == null || !name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                    {
                        continue;
                    }

                    string tail = name.Substring(prefix.Length);
                    int number;
                    if (tail.Length == 3 && IsAllDigits(tail) &&
                        Int32.TryParse(tail, NumberStyles.None, CultureInfo.InvariantCulture, out number) &&
                        number >= 2)
                    {
                        return true;
                    }
                }
            }
            catch
            {
                return false;
            }
            return false;
        }

        private static bool IsThreeDigitSuffix(string suffix, out int number)
        {
            number = -1;
            if (suffix == null || suffix.Length != 4 || suffix[0] != '.')
            {
                return false;
            }

            string digits = suffix.Substring(1);
            if (!IsAllDigits(digits))
            {
                return false;
            }
            return Int32.TryParse(digits, NumberStyles.None, CultureInfo.InvariantCulture, out number);
        }

        private static bool IsAllDigits(string text)
        {
            for (int i = 0; i < text.Length; i++)
            {
                if (!Char.IsDigit(text[i]))
                {
                    return false;
                }
            }
            return text.Length > 0;
        }
    }

    internal static class ArchiveProcessor
    {
        private const long LargeExtensionlessThreshold = 100L * 1024L * 1024L;

        public static void ExtractArchives(string folder, bool recursive, string bandizip, string password, Action<LogEntry> log)
        {
            List<string> firstArchives = FindFirstPassArchives(folder, recursive, log);
            List<string> extractedRoots = new List<string>();

            foreach (string archive in firstArchives)
            {
                string outputDir = PathRules.RemoveSuffix(archive);
                string error;
                if (RunBandizip(bandizip, archive, outputDir, password, out error))
                {
                    extractedRoots.Add(outputDir);
                    log(LogEntry.Success("1차압축풀기", archive, outputDir));
                }
                else
                {
                    log(LogEntry.Failure("1차압축풀기", archive, outputDir, "실패: " + error));
                }
            }

            List<string> secondArchives = FindSecondPassArchives(extractedRoots, log);
            HashSet<string> processed = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (string archive in secondArchives)
            {
                string prepared;
                string prepareError;
                if (!PrepareSecondPassArchive(archive, log, out prepared, out prepareError))
                {
                    log(LogEntry.Failure("2차압축보정", archive, "", "실패: " + prepareError));
                    continue;
                }

                string key = PathRules.FullPathOrOriginal(prepared);
                if (processed.Contains(key))
                {
                    continue;
                }
                processed.Add(key);

                string outputDir = Path.GetDirectoryName(prepared);
                if (String.IsNullOrEmpty(outputDir))
                {
                    outputDir = Directory.GetCurrentDirectory();
                }

                string error;
                if (RunBandizip(bandizip, prepared, outputDir, password, out error))
                {
                    log(LogEntry.Success("2차압축풀기", prepared, outputDir));
                }
                else
                {
                    log(LogEntry.Failure("2차압축풀기", prepared, outputDir, "실패: " + error));
                }
            }
        }

        private static List<string> FindFirstPassArchives(string folder, bool recursive, Action<LogEntry> log)
        {
            List<string> files = FileSystem.CollectFiles(folder, recursive, log);
            List<string> archives = new List<string>();
            foreach (string file in files)
            {
                if (!IsFirstPassArchive(file))
                {
                    continue;
                }

                int part = PartRarNumber(file);
                if (part >= 0 && part != 1)
                {
                    log(LogEntry.Skipped("1차압축풀기", file, "", "분할 rar 후속 파일"));
                    continue;
                }
                archives.Add(file);
            }
            archives.Sort(StringComparer.OrdinalIgnoreCase);
            return archives;
        }

        private static bool IsFirstPassArchive(string path)
        {
            string suffix = PathRules.GetSuffix(path);
            return String.Equals(suffix, ".7z", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(suffix, ".zip", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(suffix, ".zi", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(suffix, ".001", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(suffix, ".rar", StringComparison.OrdinalIgnoreCase);
        }

        private static List<string> FindSecondPassArchives(List<string> extractedRoots, Action<LogEntry> log)
        {
            List<string> secondArchives = new List<string>();
            HashSet<string> visitedDirs = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            foreach (string root in extractedRoots)
            {
                if (!Directory.Exists(root))
                {
                    log(LogEntry.Skipped("2차압축찾기", root, "", "압축풀기 폴더가 없습니다."));
                    continue;
                }

                List<string> dirs = FileSystem.CollectDirectories(root, log);
                foreach (string dir in dirs)
                {
                    string full = PathRules.FullPathOrOriginal(dir);
                    if (visitedDirs.Contains(full))
                    {
                        continue;
                    }
                    visitedDirs.Add(full);

                    string picked = PickLargestSecondPassArchive(dir);
                    if (picked != null)
                    {
                        secondArchives.Add(picked);
                    }
                }
            }
            return secondArchives;
        }

        private static string PickLargestSecondPassArchive(string directory)
        {
            FileInfo picked = null;
            try
            {
                DirectoryInfo dir = new DirectoryInfo(directory);
                FileInfo[] files = dir.GetFiles();
                Array.Sort(files, delegate(FileInfo left, FileInfo right)
                {
                    return StringComparer.OrdinalIgnoreCase.Compare(left.Name, right.Name);
                });

                for (int i = 0; i < files.Length; i++)
                {
                    FileInfo file = files[i];
                    if (!IsSecondPassCandidate(file))
                    {
                        continue;
                    }
                    if (picked == null || file.Length > picked.Length)
                    {
                        picked = file;
                    }
                }
            }
            catch
            {
                return null;
            }
            return picked == null ? null : picked.FullName;
        }

        private static bool IsSecondPassCandidate(FileInfo file)
        {
            string suffix = PathRules.GetSuffix(file.FullName);
            if (String.Equals(suffix, ".7z", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(suffix, ".zip", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(suffix, ".zi", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(suffix, ".rar", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(suffix, ".001", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(suffix, ".tif", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }

            if (suffix.StartsWith(".zip", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }

            return suffix.Length == 0 && file.Length > LargeExtensionlessThreshold;
        }

        internal static bool IsSecondPassCandidateForTest(string path)
        {
            return IsSecondPassCandidate(new FileInfo(path));
        }

        internal static bool PrepareSecondPassArchiveForTest(string archive, out string prepared, out string error)
        {
            return PrepareSecondPassArchive(archive, delegate { }, out prepared, out error);
        }

        private static bool PrepareSecondPassArchive(string archive, Action<LogEntry> log, out string prepared, out string error)
        {
            prepared = archive;
            error = null;

            string suffix = PathRules.GetSuffix(archive);
            bool extensionlessLarge = suffix.Length == 0 && File.Exists(archive) && new FileInfo(archive).Length > LargeExtensionlessThreshold;
            bool shouldRename = String.Equals(suffix, ".zi", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(suffix, ".tif", StringComparison.OrdinalIgnoreCase) ||
                (suffix.StartsWith(".zip", StringComparison.OrdinalIgnoreCase) && !String.Equals(suffix, ".zip", StringComparison.OrdinalIgnoreCase)) ||
                extensionlessLarge;

            if (!shouldRename)
            {
                return true;
            }

            string target = suffix.Length == 0 ? archive + ".zip" : PathRules.WithSuffix(archive, ".zip");
            try
            {
                if (PathRules.SamePath(archive, target))
                {
                    prepared = target;
                    return true;
                }
                if (File.Exists(target) || Directory.Exists(target))
                {
                    error = "변경할 대상 파일이 이미 있습니다: " + target;
                    return false;
                }

                File.Move(archive, target);
                prepared = target;
                if (log != null)
                {
                    log(LogEntry.Success("2차압축보정", archive, target));
                }
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        private static int PartRarNumber(string path)
        {
            string suffix = PathRules.GetSuffix(path);
            if (!String.Equals(suffix, ".rar", StringComparison.OrdinalIgnoreCase))
            {
                return -1;
            }

            string name = Path.GetFileName(path);
            int dot = PathRules.LastSuffixDotInName(name);
            string stem = dot >= 0 ? name.Substring(0, dot) : name;
            int number = -1;

            for (int i = 0; i < stem.Length; i++)
            {
                bool boundary = i == 0 || stem[i - 1] == '.' || stem[i - 1] == '_' || stem[i - 1] == ' ' || stem[i - 1] == '-';
                if (!boundary)
                {
                    continue;
                }
                if (i + 4 > stem.Length || String.Compare(stem, i, "part", 0, 4, true, CultureInfo.InvariantCulture) != 0)
                {
                    continue;
                }

                int pos = i + 4;
                if (pos >= stem.Length || !Char.IsDigit(stem[pos]))
                {
                    continue;
                }

                int value = 0;
                while (pos < stem.Length && Char.IsDigit(stem[pos]))
                {
                    value = value * 10 + (stem[pos] - '0');
                    pos++;
                }
                if (pos == stem.Length)
                {
                    number = value;
                }
            }

            return number;
        }

        private static bool RunBandizip(string bandizip, string archive, string outputDir, string password, out string error)
        {
            error = null;
            try
            {
                Directory.CreateDirectory(outputDir);

                ProcessStartInfo startInfo = new ProcessStartInfo();
                startInfo.FileName = bandizip;
                startInfo.Arguments = BuildBandizipArguments(archive, outputDir, password);
                startInfo.UseShellExecute = false;
                startInfo.CreateNoWindow = false;
                startInfo.WorkingDirectory = Path.GetDirectoryName(bandizip);

                using (Process process = Process.Start(startInfo))
                {
                    if (process == null)
                    {
                        error = "Bandizip 실행에 실패했습니다.";
                        return false;
                    }
                    process.WaitForExit();
                    if (process.ExitCode != 0)
                    {
                        error = "Bandizip 종료 코드 " + process.ExitCode.ToString(CultureInfo.InvariantCulture);
                        return false;
                    }
                }
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        private static string BuildBandizipArguments(string archive, string outputDir, string password)
        {
            string args = "x -aoa " + Quote("-o:" + outputDir);
            if (!String.IsNullOrEmpty(password))
            {
                args += " " + Quote("-p:" + password);
            }
            args += " " + Quote(archive);
            return args;
        }

        private static string Quote(string value)
        {
            if (value == null)
            {
                value = "";
            }

            StringBuilder quoted = new StringBuilder();
            quoted.Append('"');
            int backslashes = 0;
            for (int i = 0; i < value.Length; i++)
            {
                char ch = value[i];
                if (ch == '\\')
                {
                    backslashes++;
                    quoted.Append(ch);
                }
                else if (ch == '"')
                {
                    for (int j = 0; j <= backslashes; j++)
                    {
                        quoted.Append('\\');
                    }
                    quoted.Append('"');
                    backslashes = 0;
                }
                else
                {
                    backslashes = 0;
                    quoted.Append(ch);
                }
            }
            for (int j = 0; j < backslashes; j++)
            {
                quoted.Append('\\');
            }
            quoted.Append('"');
            return quoted.ToString();
        }
    }

    internal static class FileSystem
    {
        public static List<string> CollectFiles(string root, bool recursive, Action<LogEntry> log)
        {
            List<string> files = new List<string>();
            CollectFilesInto(root, recursive, files, log);
            files.Sort(StringComparer.OrdinalIgnoreCase);
            return files;
        }

        private static void CollectFilesInto(string root, bool recursive, List<string> files, Action<LogEntry> log)
        {
            DirectoryInfo directory;
            try
            {
                directory = new DirectoryInfo(root);
                FileInfo[] localFiles = directory.GetFiles();
                for (int i = 0; i < localFiles.Length; i++)
                {
                    files.Add(localFiles[i].FullName);
                }

                if (!recursive)
                {
                    return;
                }

                DirectoryInfo[] dirs = directory.GetDirectories();
                for (int i = 0; i < dirs.Length; i++)
                {
                    if ((dirs[i].Attributes & FileAttributes.ReparsePoint) != 0)
                    {
                        continue;
                    }
                    CollectFilesInto(dirs[i].FullName, recursive, files, log);
                }
            }
            catch (Exception ex)
            {
                log(LogEntry.Failure("파일검색", root, "", "실패: " + ex.Message));
            }
        }

        public static List<string> CollectDirectories(string root, Action<LogEntry> log)
        {
            List<string> dirs = new List<string>();
            CollectDirectoriesInto(root, dirs, log);
            return dirs;
        }

        private static void CollectDirectoriesInto(string root, List<string> dirs, Action<LogEntry> log)
        {
            dirs.Add(root);
            try
            {
                DirectoryInfo directory = new DirectoryInfo(root);
                DirectoryInfo[] children = directory.GetDirectories();
                Array.Sort(children, delegate(DirectoryInfo left, DirectoryInfo right)
                {
                    return StringComparer.OrdinalIgnoreCase.Compare(left.Name, right.Name);
                });

                for (int i = 0; i < children.Length; i++)
                {
                    if ((children[i].Attributes & FileAttributes.ReparsePoint) != 0)
                    {
                        continue;
                    }
                    CollectDirectoriesInto(children[i].FullName, dirs, log);
                }
            }
            catch (Exception ex)
            {
                log(LogEntry.Failure("폴더검색", root, "", "실패: " + ex.Message));
            }
        }
    }

    internal static class PathRules
    {
        public static bool HasSuffix(string path)
        {
            return LastSuffixDotInName(Path.GetFileName(path)) >= 0;
        }

        public static string GetSuffix(string path)
        {
            string name = Path.GetFileName(path);
            int dot = LastSuffixDotInName(name);
            return dot >= 0 ? name.Substring(dot) : "";
        }

        public static string WithSuffix(string path, string suffix)
        {
            string directory = Path.GetDirectoryName(path);
            string name = Path.GetFileName(path);
            int dot = LastSuffixDotInName(name);
            string stem = dot >= 0 ? name.Substring(0, dot) : name;
            string resultName = stem + suffix;
            return String.IsNullOrEmpty(directory) ? resultName : Path.Combine(directory, resultName);
        }

        public static string RemoveSuffix(string path)
        {
            string directory = Path.GetDirectoryName(path);
            string name = Path.GetFileName(path);
            int dot = LastSuffixDotInName(name);
            if (dot < 0)
            {
                return path;
            }
            string resultName = name.Substring(0, dot);
            return String.IsNullOrEmpty(directory) ? resultName : Path.Combine(directory, resultName);
        }

        public static bool TryGetFrontSuffixFirstChar(string path, out char first)
        {
            first = '\0';
            string name = Path.GetFileName(path);
            int last = LastSuffixDotInName(name);
            int front = PreviousSuffixDotInName(name, last);
            if (front < 0 || front + 1 >= name.Length)
            {
                return false;
            }
            first = Char.ToLowerInvariant(name[front + 1]);
            return true;
        }

        public static bool TryGetLastSuffixFirstChar(string path, out char first)
        {
            first = '\0';
            string name = Path.GetFileName(path);
            int dot = LastSuffixDotInName(name);
            if (dot < 0 || dot + 1 >= name.Length)
            {
                return false;
            }
            first = Char.ToLowerInvariant(name[dot + 1]);
            return true;
        }

        public static int LastSuffixDotInName(string name)
        {
            if (String.IsNullOrEmpty(name))
            {
                return -1;
            }
            for (int i = name.Length - 1; i >= 0; i--)
            {
                if (name[i] == '.')
                {
                    if (i != 0 && i + 1 < name.Length)
                    {
                        return i;
                    }
                }
            }
            return -1;
        }

        private static int PreviousSuffixDotInName(string name, int lastDot)
        {
            if (String.IsNullOrEmpty(name) || lastDot <= 0)
            {
                return -1;
            }
            for (int i = lastDot - 1; i >= 0; i--)
            {
                if (name[i] == '.')
                {
                    if (i != 0 && i + 1 < lastDot)
                    {
                        return i;
                    }
                }
            }
            return -1;
        }

        public static bool SamePath(string left, string right)
        {
            return String.Equals(FullPathOrOriginal(left), FullPathOrOriginal(right), StringComparison.OrdinalIgnoreCase);
        }

        public static string FullPathOrOriginal(string path)
        {
            try
            {
                return Path.GetFullPath(path);
            }
            catch
            {
                return path;
            }
        }
    }

    internal static class SelfTest
    {
        public static int Run()
        {
            int ok = 1;
            ok &= ExpectTarget(@"C:\T\26.06.mp4", @"C:\T\26.06.zip");
            ok &= ExpectTarget(@"C:\T\2026.06.MP4", @"C:\T\2026.06.zip");
            ok &= ExpectSkip(@"C:\T\신난다2026.06.mp4");
            ok &= ExpectTarget(@"C:\T\26.13.mp4", @"C:\T\26.13.zip");
            ok &= ExpectTarget(@"C:\T\abc.7foo.mp4", @"C:\T\abc.7foo");
            ok &= ExpectSkip(@"C:\T\abc.06.mp4");

            string root = Path.Combine(Path.GetTempPath(), "BaiduRenamerCsTest_" + Guid.NewGuid().ToString("N"));
            try
            {
                Directory.CreateDirectory(root);
                string disguised = Path.Combine(root, "랄랄라.7z.mp4");
                string part2 = Path.Combine(root, "랄랄라.7z.002");
                File.WriteAllText(disguised, "x");
                File.WriteAllText(part2, "x");
                ok &= ExpectTarget(disguised, Path.Combine(root, "랄랄라.7z.001"));
                ok &= ExpectSkip(part2);

                string second001 = Path.Combine(root, "inner.001");
                string tif = Path.Combine(root, "image.tif");
                string oddZip = Path.Combine(root, "신난다.zip만세");
                string extensionlessSmall = Path.Combine(root, "small");
                string extensionlessLarge = Path.Combine(root, "large");
                File.WriteAllText(second001, "x");
                File.WriteAllText(tif, "x");
                File.WriteAllText(oddZip, "x");
                File.WriteAllText(extensionlessSmall, "x");
                using (FileStream stream = File.Create(extensionlessLarge))
                {
                    stream.SetLength(100L * 1024L * 1024L + 1L);
                }

                ok &= ArchiveProcessor.IsSecondPassCandidateForTest(second001) ? 1 : 0;
                ok &= ArchiveProcessor.IsSecondPassCandidateForTest(tif) ? 1 : 0;
                ok &= ArchiveProcessor.IsSecondPassCandidateForTest(oddZip) ? 1 : 0;
                ok &= !ArchiveProcessor.IsSecondPassCandidateForTest(extensionlessSmall) ? 1 : 0;
                ok &= ArchiveProcessor.IsSecondPassCandidateForTest(extensionlessLarge) ? 1 : 0;

                string prepared;
                string error;
                ok &= ArchiveProcessor.PrepareSecondPassArchiveForTest(tif, out prepared, out error) &&
                    PathRules.SamePath(prepared, Path.Combine(root, "image.zip")) &&
                    File.Exists(prepared) ? 1 : 0;
                ok &= ArchiveProcessor.PrepareSecondPassArchiveForTest(oddZip, out prepared, out error) &&
                    PathRules.SamePath(prepared, Path.Combine(root, "신난다.zip")) &&
                    File.Exists(prepared) ? 1 : 0;
                ok &= ArchiveProcessor.PrepareSecondPassArchiveForTest(extensionlessLarge, out prepared, out error) &&
                    PathRules.SamePath(prepared, extensionlessLarge + ".zip") &&
                    File.Exists(prepared) ? 1 : 0;
            }
            catch
            {
                ok = 0;
            }
            finally
            {
                try
                {
                    if (Directory.Exists(root))
                    {
                        Directory.Delete(root, true);
                    }
                }
                catch
                {
                }
            }

            return ok == 1 ? 0 : 1;
        }

        private static int ExpectTarget(string path, string expected)
        {
            ExtensionDecision decision = ExtensionRules.Decide(path);
            return decision.ShouldChange && PathRules.SamePath(decision.Target, expected) ? 1 : 0;
        }

        private static int ExpectSkip(string path)
        {
            ExtensionDecision decision = ExtensionRules.Decide(path);
            return !decision.ShouldChange ? 1 : 0;
        }
    }
}
