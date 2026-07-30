from pathlib import Path
from datetime import datetime
import re
import sys

path = Path("worker.cc")
text = path.read_text()

backup = Path(f"worker.cc.before_chunk_size_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}")
backup.write_text(text)
print(f"[BACKUP] {backup}")

changed = False

# include 추가
if "#include <stdio.h>" not in text:
    lines = text.splitlines(True)
    last_include = None
    for i, line in enumerate(lines[:120]):
        if line.startswith("#include"):
            last_include = i
    if last_include is None:
        print("ERROR: include block not found")
        sys.exit(1)
    lines.insert(last_include + 1, "#include <stdio.h>\n")
    text = "".join(lines)
    changed = True
    print("[ADD] #include <stdio.h>")

# macro 추가
macro = r'''
#define CHUNK_SIZE_LOG(fmt, ...) do { \
    flockfile(stderr); \
    fprintf(stderr, fmt "\n", ##__VA_ARGS__); \
    funlockfile(stderr); \
} while (0)
'''

if "CHUNK_SIZE_LOG" not in text:
    lines = text.splitlines(True)
    last_include = None
    for i, line in enumerate(lines[:120]):
        if line.startswith("#include"):
            last_include = i
    if last_include is None:
        print("ERROR: include block not found for macro")
        sys.exit(1)
    lines.insert(last_include + 1, "\n" + macro + "\n")
    text = "".join(lines)
    changed = True
    print("[ADD] CHUNK_SIZE_LOG macro")
else:
    print("[SKIP] CHUNK_SIZE_LOG already exists")

# DEDUP_CHUNK 추가
dedup_log = r'''
        CHUNK_SIZE_LOG(
            "[DEDUP_CHUNK] role=%s stream_fd=%d src_len=%u chunk_len=%u flag=0x%x dup=%d",
            (app->mode == WANACC_SERVER) ? "ES" : "MS",
            s->fd,
            (unsigned)ck->src_len,
            (unsigned)ck->len,
            ck->flag,
            !!(ck->flag & CHUNK_FLAG_DUP)
        );
'''

if "[DEDUP_CHUNK]" not in text:
    pattern = re.compile(
        r'(\n[ \t]*ck[ \t]*=[ \t]*dedup_chunk_data[ \t]*\([^\;]*?&bw->ht[ \t]*\)[ \t]*;[ \t]*\n)',
        re.MULTILINE | re.DOTALL
    )
    text, n = pattern.subn(r'\1' + dedup_log, text, count=1)
    if n != 1:
        print("ERROR: dedup_chunk_data insertion point not found")
        sys.exit(1)
    changed = True
    print("[ADD] DEDUP_CHUNK log")
else:
    print("[SKIP] DEDUP_CHUNK already exists")

# back_worker_compress_send 함수 범위 찾기
m = re.search(r'static\s+void\s+back_worker_compress_send\s*\([^)]*\)\s*\{', text)
if not m:
    print("ERROR: back_worker_compress_send function not found")
    sys.exit(1)

start = m.start()
brace_start = text.find("{", m.start())
depth = 0
end = None

for i in range(brace_start, len(text)):
    if text[i] == "{":
        depth += 1
    elif text[i] == "}":
        depth -= 1
        if depth == 0:
            end = i + 1
            break

if end is None:
    print("ERROR: function end not found")
    sys.exit(1)

func = text[start:end]

# before size 변수 추가: compress if 바로 앞
before_code = r'''
        unsigned before_src_len = de->data->src_len;
        unsigned before_chunk_len = de->data->len;
        unsigned before_flag = de->data->flag;

'''

if "before_src_len" not in func:
    pattern = re.compile(
        r'(\n[ \t]*if[ \t]*\([^\n]*CHUNK_FLAG_DUP[^\n]*COMPRESSION_ENABLED[^\n]*\)[ \t]*\{)',
        re.MULTILINE
    )
    func, n = pattern.subn(before_code + r'\1', func, count=1)
    if n != 1:
        print("ERROR: compression if insertion point not found")
        sys.exit(1)
    changed = True
    print("[ADD] before-size variables")
else:
    print("[SKIP] before-size variables already exist")

# SEND_CHUNK 추가: 함수 안 첫 write_socket 바로 위
send_log = r'''
        CHUNK_SIZE_LOG(
            "[SEND_CHUNK] src_len=%u before_len=%u after_len=%u send_len=%d before_flag=0x%x after_flag=0x%x dup=%d compressed=%d",
            before_src_len,
            before_chunk_len,
            (unsigned)de->data->len,
            len,
            before_flag,
            de->data->flag,
            !!(de->data->flag & CHUNK_FLAG_DUP),
            !!(de->data->flag & CHUNK_FLAG_COMPRESSED)
        );
'''

if "[SEND_CHUNK]" not in func:
    idx = func.find("write_socket")
    if idx == -1:
        print("ERROR: write_socket not found inside back_worker_compress_send")
        print("Show this:")
        print("grep -n \"write_socket\" worker.cc")
        sys.exit(1)

    line_start = func.rfind("\n", 0, idx)
    if line_start == -1:
        print("ERROR: write_socket line start not found")
        sys.exit(1)

    func = func[:line_start+1] + send_log + func[line_start+1:]
    changed = True
    print("[ADD] SEND_CHUNK log")
else:
    print("[SKIP] SEND_CHUNK already exists")

text = text[:start] + func + text[end:]

if changed:
    path.write_text(text)
    print("[DONE] worker.cc modified")
else:
    print("[DONE] no changes needed")
