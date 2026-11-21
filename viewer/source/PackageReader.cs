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
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Xml;
using System.Xml.Linq;

namespace Wsx
{
    internal sealed class PackageReport
    {
        public readonly string Root;
        public readonly SortedSet<string> Files = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        public readonly SortedDictionary<string, string> Layout = new SortedDictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public readonly SortedDictionary<string, PeImage> Binaries = new SortedDictionary<string, PeImage>(StringComparer.OrdinalIgnoreCase);
        public readonly SortedDictionary<string, string> Executables = new SortedDictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        public readonly List<string> Images = new List<string>();
        public readonly List<string> Warnings = new List<string>();
        public string Title;
        public string Version;
        public string OsDependency;

        public PackageReport(string root)
        {
            Root = root;
        }

        private string ExecutableStatus(string path)
        {
            if (!Files.Contains(path))
                return "missing";

            PeImage image;
            return Binaries.TryGetValue(path, out image) && image.IsStandalone
                ? "PE entry point found"
                : "no standalone PE entry point";
        }

        public string Status
        {
            get
            {
                if (Executables.Count == 0)
                    return "The manifests did not declare an executable.";

                if (Executables.Keys.Any(path => Binaries.ContainsKey(path) && Binaries[path].IsStandalone))
                    return "Declared executable present. Runtime compatibility remains unverified.";

                if (Executables.Keys.All(path => !Files.Contains(path)))
                    return "Startup blocked. Missing " + string.Join(", ", Executables.Keys) + ".";

                return "Startup blocked. The declared executables have no standalone PE entry point.";
            }
        }

        public string Format()
        {
            var result = new StringBuilder();
            if (!string.IsNullOrEmpty(Title))
                result.AppendLine(Title);
            if (!string.IsNullOrEmpty(Version))
                result.AppendLine("Build " + Version);
            if (!string.IsNullOrEmpty(OsDependency))
                result.AppendLine("Declared dependency " + OsDependency);

            result.AppendLine();
            result.AppendFormat("{0} files read from the folder", Files.Count).AppendLine();
            if (Layout.Count != 0)
            {
                var missing = Layout.Where(entry => !Files.Contains(entry.Key)).ToArray();
                result.AppendFormat("{0} of {1} files declared in the layout are present",
                    Layout.Count - missing.Length, Layout.Count).AppendLine();
                foreach (var entry in missing)
                {
                    string group = string.IsNullOrEmpty(entry.Value) ? "" : " (group " + entry.Value + ")";
                    result.AppendLine("  Missing " + entry.Key + group);
                }
            }

            result.AppendLine().AppendLine("Declared executables");
            foreach (var entry in Executables)
            {
                string family = string.IsNullOrEmpty(entry.Value) ? "" : " (" + entry.Value + ")";
                result.AppendLine("  " + entry.Key + family + " · " + ExecutableStatus(entry.Key));
            }

            result.AppendLine();
            result.AppendFormat("{0} PE files, {1} DLLs, {2} standalone executables detected",
                Binaries.Count, Binaries.Values.Count(image => image.IsDll),
                Binaries.Values.Count(image => image.IsStandalone)).AppendLine();
            foreach (var entry in Binaries.Where(entry => !entry.Value.IsDll))
            {
                var image = entry.Value;
                string sectionStatus = image.HasExecutableSection ? "executable section present" : "no executable section";
                result.AppendFormat("  {0} · machine 0x{1:X4} · RVA 0x{2:X8} · {3}",
                    entry.Key, image.Machine, image.EntryPoint, sectionStatus).AppendLine();
            }

            if (Warnings.Count != 0)
            {
                result.AppendLine().AppendLine("Read warnings");
                foreach (var warning in Warnings)
                    result.AppendLine("  " + warning);
            }

            return result.ToString();
        }
    }

    internal static class PackageReader
    {
        public static PackageReport Read(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
                throw new ArgumentException("Select the build folder.");

            string root = Path.GetFullPath(path.Trim().Trim('"'));
            if (!Directory.Exists(root))
                throw new DirectoryNotFoundException("The build folder does not exist.");

            var report = new PackageReport(root);
            ReadFiles(report);
            ReadManifests(report);
            foreach (string file in report.Files.Where(IsLayout))
                ReadLayout(report, file);
            return report;
        }

        private static void ReadFiles(PackageReport report)
        {
            var pending = new Stack<string>();
            string prefix = report.Root.TrimEnd('\\', '/') + Path.DirectorySeparatorChar;
            pending.Push(report.Root);

            while (pending.Count != 0)
            {
                string directory = pending.Pop();
                string[] entries;
                try { entries = Directory.GetFileSystemEntries(directory); }
                catch (IOException error) { report.Warnings.Add(error.Message); continue; }
                catch (UnauthorizedAccessException error) { report.Warnings.Add(error.Message); continue; }

                foreach (string file in entries)
                {
                    string relative = file.Substring(prefix.Length);
                    try
                    {
                        FileAttributes attributes = File.GetAttributes(file);
                        if ((attributes & FileAttributes.ReparsePoint) != 0)
                            continue;

                        if ((attributes & FileAttributes.Directory) != 0)
                        {
                            pending.Push(file);
                            continue;
                        }

                        report.Files.Add(relative);
                        if (Path.GetExtension(relative).Equals(".png", StringComparison.OrdinalIgnoreCase))
                            report.Images.Add(relative);

                        PeImage image = PeImage.Read(file);
                        if (image != null)
                            report.Binaries.Add(relative, image);
                    }
                    catch (IOException error) { report.Warnings.Add(relative + " · " + error.Message); }
                    catch (UnauthorizedAccessException error) { report.Warnings.Add(relative + " · " + error.Message); }
                }
            }

            report.Images.Sort(StringComparer.OrdinalIgnoreCase);
        }

        private static void ReadManifests(PackageReport report)
        {
            var config = ReadDocument(report, "MicrosoftGame.Config");
            var manifest = ReadDocument(report, "appxmanifest.xml");

            report.Title = Attribute(Elements(config, "ShellVisuals").FirstOrDefault(), "DefaultDisplayName")
                ?? Elements(manifest, "DisplayName").Select(element => element.Value).FirstOrDefault();
            report.Version = Attribute(Elements(config, "Identity").FirstOrDefault(), "Version")
                ?? Attribute(Elements(manifest, "Identity").FirstOrDefault(), "Version");
            report.OsDependency = Attribute(Elements(manifest, "OSPackageDependency").FirstOrDefault(), "Name");

            foreach (var item in Elements(config, "Executable"))
                AddExecutable(report, Attribute(item, "Name"), Attribute(item, "TargetDeviceFamily"));
            foreach (var item in Elements(manifest, "Application"))
                AddExecutable(report, Attribute(item, "Executable"), null);
        }

        private static void AddExecutable(PackageReport report, string value, string family)
        {
            string relative = NormalizeRelativePath(value);
            if (!string.IsNullOrEmpty(relative) && !report.Executables.ContainsKey(relative))
                report.Executables.Add(relative, family);
        }

        private static bool IsLayout(string path)
        {
            return path.IndexOf(Path.DirectorySeparatorChar) < 0
                && path.StartsWith("layout_", StringComparison.OrdinalIgnoreCase)
                && path.EndsWith(".xml", StringComparison.OrdinalIgnoreCase);
        }

        private static void ReadLayout(PackageReport report, string file)
        {
            var document = ReadDocument(report, file);
            int unresolved = 0;
            foreach (var group in Elements(document, "FileGroup"))
            {
                string destination = NormalizeRelativePath(Attribute(group, "DestinationPath") ?? "");
                string chunk = Attribute(group.Ancestors().FirstOrDefault(node => node.Name.LocalName == "Chunk"), "Id");
                string includes = Attribute(group, "Include") ?? "";
                if (destination == null || group.Attribute("Exclude") != null)
                {
                    ++unresolved;
                    continue;
                }

                foreach (string include in includes.Split(new[] { ';' }, StringSplitOptions.RemoveEmptyEntries))
                {
                    string fileName = NormalizeRelativePath(include);
                    if (string.IsNullOrEmpty(fileName))
                    {
                        ++unresolved;
                        continue;
                    }

                    string relative = Path.Combine(destination, fileName);
                    if (!report.Layout.ContainsKey(relative))
                        report.Layout.Add(relative, chunk);
                }
            }

            if (unresolved > 0)
                report.Warnings.Add(string.Format("{0} · {1} layout filters without literal matches.", file, unresolved));
        }

        private static XDocument ReadDocument(PackageReport report, string file)
        {
            if (!report.Files.Contains(file))
                return null;

            try
            {
                var options = new XmlReaderSettings
                {
                    DtdProcessing = DtdProcessing.Prohibit,
                    XmlResolver = null,
                    MaxCharactersInDocument = 8 * 1024 * 1024
                };
                using (var stream = OpenRead(Path.Combine(report.Root, file)))
                using (var reader = XmlReader.Create(stream, options))
                    return XDocument.Load(reader);
            }
            catch (XmlException error) { report.Warnings.Add(file + " · " + error.Message); }
            catch (IOException error) { report.Warnings.Add(file + " · " + error.Message); }
            catch (UnauthorizedAccessException error) { report.Warnings.Add(file + " · " + error.Message); }
            return null;
        }

        private static IEnumerable<XElement> Elements(XDocument document, string name)
        {
            return document == null
                ? Enumerable.Empty<XElement>()
                : document.Descendants().Where(element => element.Name.LocalName == name);
        }

        private static string Attribute(XElement element, string name)
        {
            return element == null ? null : (string)element.Attribute(name);
        }

        private static string NormalizeRelativePath(string value)
        {
            if (value == null)
                return null;

            var parts = value.Trim().Replace('/', '\\')
                .Split(new[] { '\\' }, StringSplitOptions.RemoveEmptyEntries)
                .Where(part => part != ".").ToArray();

            return parts.Any(part => part == ".." || part.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
                ? null : string.Join("\\", parts);
        }

        internal static FileStream OpenRead(string path)
        {
            return new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        }
    }

    internal sealed class PeImage
    {
        private const ushort ExecutableImage = 0x0002;
        private const ushort Dll = 0x2000;
        private const uint SectionExecutable = 0x20000000;

        private ushort Flags;
        public ushort Machine { get; private set; }
        public uint EntryPoint { get; private set; }
        public bool HasExecutableSection { get; private set; }
        public bool HasEntryPointInCode { get; private set; }
        public bool IsDll { get { return (Flags & Dll) != 0; } }
        public bool IsStandalone { get { return !IsDll && (Flags & ExecutableImage) != 0 && HasEntryPointInCode; } }

        public static PeImage Read(string path)
        {
            using (var stream = PackageReader.OpenRead(path))
            using (var reader = new BinaryReader(stream))
            {
                long length = stream.Length;
                if (length < 64 || reader.ReadUInt16() != 0x5A4D)
                    return null;

                stream.Position = 0x3C;
                uint peOffset = reader.ReadUInt32();
                if (peOffset < 64 || peOffset > length - 24)
                    return null;

                stream.Position = peOffset;
                if (reader.ReadUInt32() != 0x00004550)
                    return null;

                var image = new PeImage { Machine = reader.ReadUInt16() };
                ushort sections = reader.ReadUInt16();
                stream.Position += 12;
                ushort optionalSize = reader.ReadUInt16();
                image.Flags = reader.ReadUInt16();
                long optionalStart = stream.Position;
                long sectionTable = optionalStart + optionalSize;
                if (optionalSize < 20 || sections > 96 || sectionTable + 40L * sections > length)
                    return null;

                ushort magic = reader.ReadUInt16();
                if ((magic != 0x10b && magic != 0x20b) || optionalSize < (magic == 0x10b ? 96 : 112))
                    return null;

                stream.Position = optionalStart + 16;
                image.EntryPoint = reader.ReadUInt32();

                for (int i = 0; i < sections; i++)
                {
                    stream.Position = sectionTable + 40L * i + 8;
                    uint virtualSize = reader.ReadUInt32();
                    uint virtualAddress = reader.ReadUInt32();
                    uint rawSize = reader.ReadUInt32();
                    uint rawOffset = reader.ReadUInt32();
                    stream.Position += 12;
                    uint characteristics = reader.ReadUInt32();
                    if ((characteristics & SectionExecutable) == 0)
                        continue;

                    image.HasExecutableSection = true;
                    if (image.EntryPoint == 0 || image.EntryPoint < virtualAddress)
                        continue;

                    uint offset = image.EntryPoint - virtualAddress;
                    if (offset < rawSize && (virtualSize == 0 || offset < virtualSize) && (long)rawOffset + offset < length)
                        image.HasEntryPointInCode = true;
                }

                return image;
            }
        }
    }
}
