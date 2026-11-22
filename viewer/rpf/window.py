# Copyright (c) 2026 edupode
# SPDX-License-Identifier: CPAL-1.0
"""Local Tk interface for the RPF9 explorer."""
from pathlib import Path
from queue import Empty, Queue
from threading import Thread
import tkinter as tk
from tkinter import filedialog, ttk
from tkinter.scrolledtext import ScrolledText


def open_explorer(initial, offset, header, read_page, inspect, page_bytes):
    window = tk.Tk()
    window.title("AnyXboxSeries · RPF9 Explorer")
    window.geometry("1060x650")
    window.minsize(760,430)
    frame = ttk.Frame(window, padding=12)
    frame.pack(fill="both",expand=True)
    bar = ttk.Frame(frame)
    bar.pack(fill="x")
    files = ttk.Combobox(bar,state="readonly",width=40)
    files.pack(side="left",fill="x",expand=True,padx=(0,8))
    address = tk.StringVar()
    status = tk.StringVar(value="Select an archive or its package directory.")
    search = tk.StringVar()
    paths, entries = [], []
    events = Queue()
    generation = 0
    current_start = 0
    requested = offset
    next_start = 0
    end_file = 0
    first = 0
    record_page = 0
    table_size = 400

    def show_details(value):
        details.configure(state="normal")
        details.delete("1.0","end")
        details.insert("1.0",value)
        details.configure(state="disabled")

    def present():
        nonlocal record_page
        rows.delete(*rows.get_children())
        needle = search.get().casefold().strip()
        ids = [i for i,e in enumerate(entries) if not needle or needle in f"{e.kind} {e.detail} 0x{e.offset:08X}".casefold()]
        record_page = min(record_page,max(0,(len(ids)-1)//table_size))
        start=record_page*table_size
        for i in ids[start:start+table_size]:
            e=entries[i]
            rows.insert("","end",iid=str(i),values=(f"0x{e.offset:08X}",e.storage,e.detail))
        status.set(f"0x{current_start:08X} – 0x{next_start:08X} of 0x{end_file:X}  ·  {len(entries):,} streams or headers  ·  {len(ids):,} match  ·  results page {record_page+1}/{max(1,(len(ids)+table_size-1)//table_size)}")

    def take_page(delta):
        nonlocal record_page
        record_page=max(0,record_page+delta)
        present()

    def queue_work(callback,kind):
        nonlocal generation
        generation+=1
        revision=generation
        def worker():
            try:
                result=callback()
                events.put((revision,kind,result))
            except Exception as error:
                events.put((revision,"error",str(error)))
        Thread(target=worker,daemon=True).start()

    def page_at(pos):
        nonlocal current_start
        if not paths:
            return
        p=paths[files.current()]
        current_start=pos
        address.set(f"0x{pos:X}")
        rows.delete(*rows.get_children())
        entries.clear()
        show_details("")
        status.set(f"Reading {p.name} at 0x{pos:X}…")
        queue_work(lambda:read_page(p,pos),"page")

    def select_file(event=None):
        nonlocal first,requested
        try:
            first,_=header(paths[files.current()])
            pos=first if requested is None else requested
            requested=None
            page_at(pos)
        except (ValueError,OSError) as error:
            status.set(str(error))

    def choose(path):
        nonlocal paths,generation
        if not path:
            return
        p=Path(path)
        generation+=1
        rows.delete(*rows.get_children())
        entries.clear()
        show_details("")
        paths=sorted(p.rglob("*.[rR][pP][fF]")) if p.is_dir() else [p]
        paths=[q for q in paths if q.is_file()]
        files.configure(values=[str(q.relative_to(p)) if p.is_dir() else q.name for q in paths])
        if not paths:
            files.set("")
            status.set("No RPF archives found.")
            return
        files.current(next((i for i,v in enumerate(paths) if v.name.lower()=="data.rpf"),0))
        select_file()

    def jump():
        try:
            page_at(int(address.get().strip(),0))
        except ValueError:
            status.set("Enter a decimal or hexadecimal offset, for example 0x11E18000.")

    def selected(event=None):
        sel=rows.selection()
        if sel and paths:
            entry=entries[int(sel[0])]
            p=paths[files.current()]
            show_details("Reading the selected stream…")
            queue_work(lambda:inspect(p,entry.offset),"detail")

    def poll():
        nonlocal next_start,end_file,record_page
        try:
            while True:
                revision,kind,result=events.get_nowait()
                if revision!=generation:
                    continue
                if kind=="error":
                    status.set(result)
                    show_details(result)
                elif kind=="detail":
                    show_details(result)
                else:
                    batch,next_start,end_file=result
                    entries[:] = batch
                    record_page=0
                    present()
                    if rows.get_children():
                        item=rows.get_children()[0]
                        rows.selection_set(item)
        except Empty:
            pass
        window.after(80,poll)

    def button(parent,label,callback):
        widget=ttk.Button(parent,text=label,command=callback)
        widget.pack(side="left",padx=2)
        return widget

    button(bar,"Open RPF…",lambda:choose(filedialog.askopenfilename(filetypes=[("RPF archives","*.rpf"),("All files","*.*")])) )
    button(bar,"Package folder…",lambda:choose(filedialog.askdirectory()))
    files.bind("<<ComboboxSelected>>",select_file)
    nav=ttk.Frame(frame)
    nav.pack(fill="x",pady=8)
    ttk.Label(nav,text="Archive offset").pack(side="left",padx=(0,4))
    entry=ttk.Entry(nav,textvariable=address,width=14)
    entry.pack(side="left")
    entry.bind("<Return>",lambda _:jump())
    button(nav,"Go",jump)
    button(nav,"Previous 8 MiB",lambda:page_at(max(first,current_start-page_bytes)))
    button(nav,"Next 8 MiB",lambda:page_at(next_start) if next_start<end_file else None)
    ttk.Label(nav,text="Filter range").pack(side="left",padx=(12,4))
    ttk.Entry(nav,textvariable=search,width=20).pack(side="left",fill="x",expand=True)
    search.trace_add("write",lambda *_:take_page(-record_page))
    pane=ttk.Panedwindow(frame,orient="horizontal")
    pane.pack(fill="both",expand=True)
    left=ttk.Frame(pane)
    right=ttk.Frame(pane)
    pane.add(left,weight=3)
    pane.add(right,weight=2)
    rows=ttk.Treeview(left,columns=("offset","storage","type"),show="headings",selectmode="browse")
    for name,label,width in (("offset","Offset",106),("storage","Storage",62),("type","Resource or stream",254)):
        rows.heading(name,text=label)
        rows.column(name,width=width,stretch=name=="type")
    scrollbar=ttk.Scrollbar(left,orient="vertical",command=rows.yview)
    rows.configure(yscrollcommand=scrollbar.set)
    scrollbar.pack(side="right",fill="y")
    rows.pack(fill="both",expand=True)
    rows.bind("<<TreeviewSelect>>",selected)
    details=ScrolledText(right,wrap="word",width=42,state="disabled",padx=8,pady=6)
    details.pack(fill="both",expand=True)
    footer=ttk.Frame(frame)
    footer.pack(fill="x",pady=(8,0))
    button(footer,"‹ Results",lambda:take_page(-1))
    button(footer,"Results ›",lambda:take_page(1))
    ttk.Label(footer,textvariable=status,wraplength=670).pack(side="left",padx=6)
    window.after(80,poll)
    if initial:
        window.after_idle(lambda:choose(initial))
    window.mainloop()
