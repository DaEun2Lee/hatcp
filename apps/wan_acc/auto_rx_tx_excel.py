import re
import csv
import time
import sys
import zipfile
from pathlib import Path
from xml.sax.saxutils import escape

if len(sys.argv) != 2 or sys.argv[1] not in ("ms", "es"):
    raise SystemExit("usage: python3 auto_rx_tx_excel.py ms|es")

role = sys.argv[1]

log_path = Path("/tmp/wanacc_chunk_size.txt")
xlsx_path = Path(f"/tmp/{role}_rx_tx_8_values.xlsx")
csv_path = Path(f"/tmp/{role}_rx_tx_8_values.csv")

labels = [
    ("rx_dedup_before", "받을때_중복_before"),
    ("rx_dedup_after",  "받을때_중복_after"),
    ("rx_comp_before",  "받을때_압축_before"),
    ("rx_comp_after",   "받을때_압축_after"),
    ("tx_dedup_before", "보낼때_중복_before"),
    ("tx_dedup_after",  "보낼때_중복_after"),
    ("tx_comp_before",  "보낼때_압축_before"),
    ("tx_comp_after",   "보낼때_압축_after"),
]

def parse_log():
    raw = log_path.read_text(errors="ignore").replace("\\n", "\n")

    v = {
        "rx_dedup_before": 0,
        "rx_dedup_after": 0,
        "rx_comp_before": 0,
        "rx_comp_after": 0,
        "tx_dedup_before": 0,
        "tx_dedup_after": 0,
        "tx_comp_before": 0,
        "tx_comp_after": 0,
    }

    for line in raw.splitlines():
        m = re.search(r'\[RESTORE_DEDUP\].*?before_len=(\d+).*?after_len=(\d+)', line)
        if m:
            v["rx_dedup_before"] += int(m.group(1))
            v["rx_dedup_after"] += int(m.group(2))

        m = re.search(r'\[RESTORE_COMP\].*?before_len=(\d+).*?after_len=(\d+)', line)
        if m:
            v["rx_comp_before"] += int(m.group(1))
            v["rx_comp_after"] += int(m.group(2))

        m = re.search(r'\[DEDUP_CHUNK\].*?src_len=(\d+).*?chunk_len=(\d+)', line)
        if m:
            v["tx_dedup_before"] += int(m.group(1))
            v["tx_dedup_after"] += int(m.group(2))

        m = re.search(r'\[SEND_CHUNK\].*?before_len=(\d+).*?after_len=(\d+)', line)
        if m:
            v["tx_comp_before"] += int(m.group(1))
            v["tx_comp_after"] += int(m.group(2))

    return v

def cell(r, c, val):
    col = chr(64 + c)
    ref = f"{col}{r}"
    if isinstance(val, int):
        return f'<c r="{ref}"><v>{val}</v></c>'
    return f'<c r="{ref}" t="inlineStr"><is><t>{escape(str(val))}</t></is></c>'

def write_xlsx(v):
    rows = [["노드", "항목", "값(bytes)"]]
    for key, label in labels:
        rows.append([role.upper(), label, v[key]])

    sheet_rows = []
    for r_idx, row in enumerate(rows, 1):
        sheet_rows.append(
            f'<row r="{r_idx}">' +
            ''.join(cell(r_idx, c_idx, val) for c_idx, val in enumerate(row, 1)) +
            '</row>'
        )

    sheet_xml = f'''<?xml version="1.0" encoding="UTF-8"?>
<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">
<cols>
<col min="1" max="1" width="14" customWidth="1"/>
<col min="2" max="2" width="30" customWidth="1"/>
<col min="3" max="3" width="18" customWidth="1"/>
</cols>
<sheetData>{''.join(sheet_rows)}</sheetData>
</worksheet>
'''

    tmp_path = xlsx_path.with_suffix(".xlsx.tmp")

    with zipfile.ZipFile(tmp_path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", '''<?xml version="1.0"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/>
<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>
<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>
</Types>''')
        z.writestr("_rels/.rels", '''<?xml version="1.0"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>
</Relationships>''')
        z.writestr("xl/workbook.xml", f'''<?xml version="1.0"?>
<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"
xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">
<sheets><sheet name="{role}_8_values" sheetId="1" r:id="rId1"/></sheets>
</workbook>''')
        z.writestr("xl/_rels/workbook.xml.rels", '''<?xml version="1.0"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>
</Relationships>''')
        z.writestr("xl/worksheets/sheet1.xml", sheet_xml)

    tmp_path.replace(xlsx_path)

def write_csv(v):
    tmp_path = csv_path.with_suffix(".csv.tmp")
    with open(tmp_path, "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(["노드", "항목", "값(bytes)"])
        for key, label in labels:
            w.writerow([role.upper(), label, v[key]])
    tmp_path.replace(csv_path)

print(f"[AUTO] {role.upper()} watcher started", flush=True)
print(f"[AUTO] xlsx = {xlsx_path}", flush=True)
print(f"[AUTO] csv  = {csv_path}", flush=True)

last_state = None

while True:
    try:
        if not log_path.exists():
            time.sleep(1)
            continue

        st = log_path.stat()
        state = (st.st_mtime_ns, st.st_size)

        if state != last_state:
            v = parse_log()
            write_xlsx(v)
            write_csv(v)
            last_state = state
            print(f"[UPDATED] {xlsx_path} / {csv_path}", flush=True)
            for key, label in labels:
                print(f"{label} = {v[key]}", flush=True)

    except Exception as e:
        print(f"[ERR] {e}", flush=True)

    time.sleep(2)
