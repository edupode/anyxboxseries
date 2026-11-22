# RPF9 Explorer

Run `Open-RPF.cmd` with a Windows installation of Python 3.10 or newer that includes Tk. Use **Open RPF** for a single archive or **Package folder** for a list of local archives. Select a row to inspect its contents. The offset, range navigation and filter act on the readable data area of the selected file. Each range covers 8 MiB of stream start positions. A stream can continue past that range.

From the repository root:

~~~powershell
python tools/rpf_explorer.py 'C:\Games\Package'
python tools/rpf_explorer.py 'C:\Games\Package\data.rpf' --offset 0x11E18000
python tools/rpf_explorer.py 'C:\Games\Package\data.rpf' --offset 0x11E18000 --detail
python tools/rpf_explorer.py 'C:\Games\Package\script.rpf' --list
~~~

The command-line examples include the minimap prefab offset observed in the 1.0.2.1 local build. Other builds use different offsets. Entries show validated zlib streams and recognized PNG or Bink headers. A zlib row reports its compressed start offset. Its details identify RBF0 roots, PSIN v22 candidates or media headers in the inflated data when present. Raw PNG rows are checked against the IHDR CRC. Bink rows report metadata from a plausible header. File names and directory paths inside an encrypted archive index are unavailable in this view.
