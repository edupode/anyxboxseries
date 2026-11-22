// Copyright (c) 2026 edupode
// SPDX-License-Identifier: CPAL-1.0
//
// The contents of this file are subject to the Common Public Attribution License Version 1.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at https://opensource.org/license/CPAL-1.0. The License is based on the Mozilla Public License Version 1.1 but Sections 14 and 15 have been added to cover use of software over a computer network and provide for limited attribution for the Original Developer. In addition, Exhibit A has been modified to be consistent with Exhibit B.
// Software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License for the specific language governing rights and limitations under the License.
// The Original Code is AnyXboxSeries.
// The Original Developer is the Initial Developer.
// The Initial Developer of the Original Code is edupode. All portions of the code written by edupode are Copyright (c) 2026 edupode. All Rights Reserved.
//

using System;
using System.Drawing;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace Wsx
{
    internal static class Launcher
    {
        [STAThread]
        private static int Main(string[] args)
        {
            if (args.Length > 0 && (args[0].Equals("--diagnostics", StringComparison.OrdinalIgnoreCase) ||
                args[0].Equals("--diagnostico", StringComparison.OrdinalIgnoreCase)))
                return RunDiagnostic(args);

            string folder = args.FirstOrDefault() ?? "";
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            using (var form = new LauncherForm(folder))
                Application.Run(form);
            return 0;
        }

        private static int RunDiagnostic(string[] args)
        {
            var encoding = new UTF8Encoding(false);
            Console.SetOut(new StreamWriter(Console.OpenStandardOutput(), encoding) { AutoFlush = true });
            Console.SetError(new StreamWriter(Console.OpenStandardError(), encoding) { AutoFlush = true });
            if (args.Length != 2)
            {
                Console.Error.WriteLine("Usage  WSX-Resource-Launcher.exe --diagnostics <folder>");
                return 2;
            }

            try
            {
                var report = PackageReader.Read(args[1]);
                Console.WriteLine(report.Status);
                Console.Write(report.Format());
                Console.WriteLine();
                Console.WriteLine("{0} PNG images", report.Images.Count);
                bool unreadable = false;
                foreach (string relative in report.Images)
                {
                    string path = Path.Combine(report.Root, relative);
                    try
                    {
                        using (var image = OpenPreview(path))
                            Console.WriteLine("  {0} · {1} × {2}", relative, image.Width, image.Height);
                    }
                    catch (Exception error)
                    {
                        if (!IsPreviewError(error))
                            throw;

                        Console.Error.WriteLine("  {0} · {1}", relative, error.Message);
                        unreadable = true;
                    }
                }

                return unreadable ? 1 : 0;
            }
            catch (Exception error)
            {
                if (!Launcher.IsReadError(error))
                    throw;

                Console.Error.WriteLine(error.Message);
                return 1;
            }
        }

        internal static bool IsReadError(Exception error)
        {
            return error is IOException || error is UnauthorizedAccessException || error is ArgumentException;
        }

        internal static bool IsPreviewError(Exception error)
        {
            return error is ArgumentException || error is IOException || error is UnauthorizedAccessException
                || error is ExternalException || error is OutOfMemoryException;
        }

        internal static Bitmap OpenPreview(string path)
        {
            using (var stream = PackageReader.OpenRead(path))
            using (var source = Image.FromStream(stream, false, true))
                return new Bitmap(source);
        }
    }

    internal sealed class LauncherForm : Form
    {
        private readonly TextBox folder = new TextBox();
        private readonly Button choose = new Button { Text = "Choose folder", AutoSize = true };
        private readonly Button analyze = new Button { Text = "Analyze", AutoSize = true };
        private readonly Button exploreRpf = new Button { Text = "Explore RPF archives", AutoSize = true };
        private readonly Button flyMap = new Button { Text = "Fly through 3D map", AutoSize = true };
        private readonly Label status = new Label { AutoSize = true };
        private readonly RichTextBox diagnostic = new RichTextBox
        {
            Dock = DockStyle.Fill,
            BorderStyle = BorderStyle.None,
            ReadOnly = true,
            DetectUrls = false,
            BackColor = Color.White,
            Font = new Font("Consolas", 10F),
            WordWrap = true
        };
        private readonly ListBox imageFiles = new ListBox { Dock = DockStyle.Fill, IntegralHeight = false };
        private readonly PictureBox preview = new PictureBox
        {
            Dock = DockStyle.Fill,
            SizeMode = PictureBoxSizeMode.Zoom,
            BackColor = Color.FromArgb(24, 29, 38)
        };
        private readonly Label imageDetails = new Label
        {
            Dock = DockStyle.Bottom,
            Height = 28,
            TextAlign = ContentAlignment.MiddleLeft,
            Padding = new Padding(6, 0, 6, 0)
        };
        private PackageReport current;

        public LauncherForm(string initialFolder)
        {
            Text = "AnyXboxSeries Resource Viewer";
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(960, 640);
            MinimumSize = new Size(640, 420);
            Font = new Font("Segoe UI", 10F);
            AutoScaleMode = AutoScaleMode.Dpi;

            var root = new TableLayoutPanel
            {
                Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 6,
                Padding = new Padding(16), BackColor = Color.FromArgb(245, 247, 250)
            };
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            root.RowStyles.Add(new RowStyle(SizeType.AutoSize));

            var pathRow = new TableLayoutPanel { Dock = DockStyle.Top, AutoSize = true, ColumnCount = 3 };
            pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            pathRow.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            folder.Dock = DockStyle.Fill;
            folder.Text = initialFolder;
            folder.AccessibleName = "Build folder";
            pathRow.Controls.Add(folder, 0, 0);
            pathRow.Controls.Add(choose, 1, 0);
            pathRow.Controls.Add(analyze, 2, 0);
            status.Text = "Select the build folder.";
            status.Margin = new Padding(3, 6, 3, 12);

            var toolsRow = new FlowLayoutPanel
            {
                Dock = DockStyle.Top,
                AutoSize = true,
                WrapContents = true,
                Margin = new Padding(0, 0, 0, 10)
            };
            exploreRpf.AccessibleName = "Open the RPF archive explorer";
            flyMap.AccessibleName = "Open the local 3D flyover scene";
            toolsRow.Controls.Add(exploreRpf);
            toolsRow.Controls.Add(flyMap);

            var tabs = new TabControl { Dock = DockStyle.Fill };
            var auditTab = new TabPage("Diagnostics") { Padding = new Padding(10), BackColor = Color.White };
            auditTab.Controls.Add(diagnostic);
            var previewsTab = new TabPage("Images") { Padding = new Padding(5) };
            var split = new SplitContainer
            {
                Dock = DockStyle.Fill, Size = new Size(900, 440), FixedPanel = FixedPanel.Panel1,
                Panel1MinSize = 145, Panel2MinSize = 200, SplitterDistance = 220
            };
            imageFiles.AccessibleName = "PNG images";
            split.Panel1.Controls.Add(imageFiles);
            split.Panel2.Controls.Add(preview);
            split.Panel2.Controls.Add(imageDetails);
            previewsTab.Controls.Add(split);
            tabs.TabPages.Add(auditTab);
            tabs.TabPages.Add(previewsTab);

            root.Controls.Add(new Label { AutoSize = true, Text = "Build folder" }, 0, 0);
            root.Controls.Add(pathRow, 0, 1);
            root.Controls.Add(status, 0, 2);
            root.Controls.Add(toolsRow, 0, 3);
            root.Controls.Add(tabs, 0, 4);
            root.Controls.Add(new Label
            {
                AutoSize = true,
                Margin = new Padding(3, 10, 3, 0),
                Text = "AnyXboxSeries by edupode   |   Copyright (c) 2026 edupode\r\n"
                    + "https://github.com/edupode/anyxboxseries"
            }, 0, 5);
            Controls.Add(root);

            AcceptButton = analyze;
            choose.Click += async delegate
            {
                using (var dialog = new FolderBrowserDialog { Description = "Select the build folder", ShowNewFolderButton = false })
                {
                    if (Directory.Exists(folder.Text))
                        dialog.SelectedPath = folder.Text;

                    if (dialog.ShowDialog(this) == DialogResult.OK)
                    {
                        folder.Text = dialog.SelectedPath;
                        await AnalyzeAsync();
                    }
                }
            };
            analyze.Click += async delegate { await AnalyzeAsync(); };
            exploreRpf.Click += delegate { OpenRpfExplorer(); };
            flyMap.Click += delegate { OpenFlyover(); };
            imageFiles.SelectedIndexChanged += delegate { ShowPreview(); };
            Shown += async delegate
            {
                if (Directory.Exists(folder.Text))
                    await AnalyzeAsync();
            };
        }

        private static string FindRepositoryFile(string relativePath)
        {
            DirectoryInfo directory = new DirectoryInfo(Application.StartupPath);
            for (int depth = 0; directory != null && depth < 8; depth++, directory = directory.Parent)
            {
                string candidate = Path.Combine(directory.FullName, relativePath);
                if (File.Exists(candidate))
                    return candidate;
            }

            return null;
        }

        private static string FindExecutableInPath(string executable)
        {
            foreach (string entry in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(Path.PathSeparator))
            {
                string directory = entry.Trim().Trim('"');
                if (directory.Length == 0)
                    continue;

                string candidate = Path.Combine(directory, executable);
                if (File.Exists(candidate))
                    return candidate;
            }

            return null;
        }

        private static string FindBlender()
        {
            string fromPath = FindExecutableInPath("blender.exe");
            if (fromPath != null)
                return fromPath;

            string[] roots =
            {
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs"),
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
                Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86)
            };
            foreach (string root in roots.Where(Directory.Exists))
            {
                foreach (string product in Directory.GetDirectories(root, "Blender*"))
                {
                    string direct = Path.Combine(product, "blender.exe");
                    if (File.Exists(direct))
                        return direct;

                    foreach (string version in Directory.GetDirectories(product))
                    {
                        string nested = Path.Combine(version, "blender.exe");
                        if (File.Exists(nested))
                            return nested;
                    }
                }

                string foundation = Path.Combine(root, "Blender Foundation");
                if (Directory.Exists(foundation))
                {
                    foreach (string version in Directory.GetDirectories(foundation, "Blender*"))
                    {
                        string candidate = Path.Combine(version, "blender.exe");
                        if (File.Exists(candidate))
                            return candidate;
                    }
                }
            }

            return null;
        }

        private static string QuoteArgument(string value)
        {
            string root = Path.GetPathRoot(value) ?? "";
            string trimmed = value.Length > root.Length ? value.TrimEnd('\\', '/') : value;
            return "\"" + trimmed.Replace("\"", "\\\"") + "\"";
        }

        private void OpenRpfExplorer()
        {
            string script = FindRepositoryFile(Path.Combine("tools", "rpf_explorer.py"));
            if (script == null)
            {
                MessageBox.Show(this, "The RPF explorer could not be found beside this launcher.",
                    "RPF explorer", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }

            string python = FindExecutableInPath("pythonw.exe") ?? FindExecutableInPath("python.exe");
            if (python == null)
            {
                MessageBox.Show(this, "Python 3.10 or newer is required to open the RPF explorer.",
                    "RPF explorer", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            string package = Directory.Exists(folder.Text) ? folder.Text.Trim() : "";
            string arguments = QuoteArgument(script) + (package.Length == 0 ? "" : " " + QuoteArgument(package));
            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = python,
                    Arguments = arguments,
                    UseShellExecute = false,
                    CreateNoWindow = true
                });
            }
            catch (Exception error)
            {
                if (!(error is IOException || error is InvalidOperationException || error is System.ComponentModel.Win32Exception))
                    throw;

                MessageBox.Show(this, "Could not start the RPF explorer. " + error.Message,
                    "RPF explorer", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void OpenFlyover()
        {
            string scene = FindRepositoryFile(Path.Combine("build", "local-map", "voo-urbano.blend"));
            if (scene == null)
                scene = FindRepositoryFile(Path.Combine("local-map", "voo-urbano.blend"));
            if (scene == null)
            {
                using (var dialog = new OpenFileDialog
                {
                    Title = "Select a Blender flyover scene",
                    Filter = "Blender scene (*.blend)|*.blend",
                    CheckFileExists = true
                })
                {
                    if (dialog.ShowDialog(this) != DialogResult.OK)
                        return;
                    scene = dialog.FileName;
                }
            }

            string blender = FindBlender();
            if (blender == null)
            {
                MessageBox.Show(this, "Install Blender or add blender.exe to PATH to open the 3D scene.",
                    "3D flyover", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            try
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = blender,
                    Arguments = QuoteArgument(scene),
                    UseShellExecute = true
                });
            }
            catch (Exception error)
            {
                if (!(error is IOException || error is InvalidOperationException || error is System.ComponentModel.Win32Exception))
                    throw;

                MessageBox.Show(this, "Could not open the Blender scene. " + error.Message,
                    "3D flyover", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private async Task AnalyzeAsync()
        {
            string path = folder.Text;
            analyze.Enabled = choose.Enabled = folder.Enabled = false;
            status.Text = "Reading the build…";
            current = null;
            imageFiles.Items.Clear();
            diagnostic.Clear();
            ShowPreview();

            try
            {
                var report = await Task.Run(() => PackageReader.Read(path));
                if (IsDisposed || Disposing)
                    return;

                current = report;
                status.Text = report.Status;
                diagnostic.Text = report.Format();
                imageFiles.Items.AddRange(report.Images.ToArray());
                if (imageFiles.Items.Count > 0)
                {
                    int splashIndex = report.Images.FindIndex(file =>
                        Path.GetFileName(file).Equals("xbs_splash.png", StringComparison.OrdinalIgnoreCase));
                    imageFiles.SelectedIndex = Math.Max(0, splashIndex);
                }
            }
            catch (Exception error)
            {
                if (!Launcher.IsReadError(error))
                    throw;

                if (!IsDisposed && !Disposing)
                    status.Text = error.Message;
            }
            finally
            {
                if (!IsDisposed && !Disposing)
                    analyze.Enabled = choose.Enabled = folder.Enabled = true;
            }
        }

        private void ShowPreview()
        {
            var old = preview.Image;
            preview.Image = null;
            if (old != null)
                old.Dispose();

            imageDetails.Text = "";
            string relative = imageFiles.SelectedItem as string;
            if (current == null || relative == null)
                return;

            try
            {
                Bitmap image = Launcher.OpenPreview(Path.Combine(current.Root, relative));
                preview.Image = image;
                imageDetails.Text = string.Format("{0}   {1} × {2}", Path.GetFileName(relative), image.Width, image.Height);
            }
            catch (Exception error)
            {
                if (!Launcher.IsPreviewError(error))
                    throw;

                imageDetails.Text = "Could not open " + Path.GetFileName(relative) + ".";
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing && preview.Image != null)
            {
                preview.Image.Dispose();
                preview.Image = null;
            }

            base.Dispose(disposing);
        }
    }
}
