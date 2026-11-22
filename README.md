# SQLog

ไลบรารี C++ แบบ header-only สำหรับ **เขียน log** และ **ค้นหา log ด้วยภาษา query สไตล์ SQL** พร้อมโปรแกรมตัวอย่าง (`src/main.cpp`) ที่รับพารามิเตอร์จาก command line

- เขียน log แบบ thread-safe — 1 log = 1 บรรทัดเสมอ
- หมุนไฟล์ (log rotation) อัตโนมัติเมื่อไฟล์เต็ม เลือกเก็บไฟล์เก่าไว้ใน `archive/` ได้
- โหลดไฟล์ log เข้าหน่วยความจำ แล้วสั่ง `search`, `count`, `delete`, `head`, `tail` ฯลฯ ได้เหมือน SQL
- parser เขียนด้วย [cpp-peglib](https://github.com/yhirose/cpp-peglib) แก้ไวยากรณ์ได้ง่าย
- ตั้งค่าจาก `config/config.json` และ override ได้ด้วย argument ตอนรัน

---

## สารบัญ

1. [ภาพรวมสถาปัตยกรรม](#ภาพรวมสถาปัตยกรรม)
2. [โครงสร้างโปรเจกต์](#โครงสร้างโปรเจกต์)
3. [ความต้องการของระบบ](#ความต้องการของระบบ)
4. [Build](#build)
5. [รูปแบบไฟล์ log](#รูปแบบไฟล์-log)
6. [เริ่มต้นใช้งานอย่างเร็ว](#เริ่มต้นใช้งานอย่างเร็ว)
7. [โปรแกรมตัวอย่าง (`main.cpp`) และ Command line](#โปรแกรมตัวอย่าง-maincpp-และ-command-line)
8. [การตั้งค่า](#การตั้งค่า)
9. [API Reference](#api-reference)
10. [ภาษา Query](#ภาษา-query)
11. [ตัวอย่าง Query](#ตัวอย่าง-query)
12. [Error code](#error-code)
13. [ข้อควรรู้ / ข้อจำกัด](#ข้อควรรู้--ข้อจำกัด)
14. [Troubleshooting](#troubleshooting)

---

## ภาพรวมสถาปัตยกรรม

```
            ┌────────────────────────┐
 append()   │      sqlog.hpp         │   เขียน [Tag] [time] [config] "content"
 logInfo()─▶│  mutex + rotation      │──▶ test/logs/main.log
            └────────────────────────┘             │
                                                   │ parse()
            ┌────────────────────────┐             ▼
 doString()─▶│      engine.hpp        │◀── LogBuffer (vector<LogLine>)
            │ PEG parser → AST →     │
            │ expression tree → eval │──▶ ตารางใน stdout + SQLogResult
            └────────────────────────┘
```

| ไฟล์ | หน้าที่ |
|---|---|
| `sqlog.hpp` | ฝั่ง **เขียน**: เปิดโฟลเดอร์ log, อ่าน `init.json`, `append`, หมุนไฟล์ |
| `engine.hpp` | ฝั่ง **อ่าน/ค้นหา**: parse ไฟล์ log, ภาษา query, พิมพ์ตาราง |
| `main.cpp` | โปรแกรมตัวอย่าง: อ่าน config, รับ argument, เรียกใช้ engine |

---

## โครงสร้างโปรเจกต์

```
SQLog
├── bin/run.out              # ไฟล์ที่ build ได้
├── config/config.json       # config ของโปรแกรม (อ่านตอนเริ่มรัน)
├── example/                 # ตัวอย่างการใช้งาน
├── makefile
├── src
│   ├── main.cpp
│   └── include
│       ├── SQLog
│       │   ├── sqlog.hpp
│       │   └── engine.hpp
│       ├── nlohmann/        # nlohmann/json
│       └── peglib.h         # cpp-peglib
└── test/logs
    ├── init.json            # config ของระบบ log
    └── main.log
```

> `luajit-2.1`, `raylib`, `Mono_c11.hpp` ใน `src/include` ไม่ได้ถูก SQLog ใช้

---

## ความต้องการของระบบ

- คอมไพเลอร์ที่รองรับ **C++20** (ใช้ `<format>`, `std::map::contains`) เช่น GCC 13+
- รองรับ Linux / WSL / Windows (มี `localtime_s` / `localtime_r` แยกตามแพลตฟอร์ม)
- ไลบรารีที่ต้องใช้ (header-only, มีอยู่ใน `src/include` แล้ว): `nlohmann/json`, `cpp-peglib`
- ต้องลิงก์ `-pthread` (peglib และ `std::mutex`)

---

## Build

```bash
make
./bin/run.out
```

หรือคอมไพล์เองโดยตรง:

```bash
g++ -std=c++20 -O2 -pthread -Isrc/include src/main.cpp -o bin/run.out
```

> ตัวอย่างข้างบนเป็นคำสั่งทั่วไป ถ้า `makefile` ของคุณใช้ flag อื่นให้ยึดตาม makefile

**สำคัญ:** `main.cpp` หาโฟลเดอร์ root ของโปรเจกต์จาก path ของไฟล์ที่รัน (`bin/run.out` → ขึ้นไป 2 ระดับ) จึงควรรันโปรแกรมที่อยู่ใน `bin/` ของโปรเจกต์ และ `config/config.json` ต้องอยู่ที่ root

---

## รูปแบบไฟล์ log

ทุกบรรทัดมีรูปแบบ:

```
[Tag] [time] [config] "content"
```

ตัวอย่าง:

```
[INFO] [2026-11-22T10:15:03] [network] "connected to server"
[ERROR] [2026-11-22T10:15:09] [network] "timeout after 30s"
[WARN] [2026-11-22T10:16:00] [db] "slow query: \"SELECT *\""
```

| ส่วน | ความหมาย | กฎ |
|---|---|---|
| `Tag` | ระดับ/ชนิดของ log เช่น `INFO`, `WARN`, `ERROR`, `DEBUG` | ห้ามมีช่องว่างและ `]` |
| `time` | เวลาที่บันทึก | รูปแบบ `YYYY-MM-DDTHH:MM:SS` |
| `config` | แหล่งที่มา / โมดูล / ชื่อผู้ใช้ | ห้ามมีช่องว่างและ `]` |
| `content` | ข้อความ | อยู่ใน `"..."` escape ด้วย `\"`, `\\`, `\n`, `\r` |

ตอนเขียน SQLog จัดการให้เอง:

- ช่องว่าง/tab/ขึ้นบรรทัดใหม่ใน tag และ config ถูกยุบเป็น `_` ตัวเดียว
- `[` และ `]` ถูกแทนด้วย `(` และ `)`
- ค่าว่างจะกลายเป็น `-`
- `content` ถูก escape ให้เป็นบรรทัดเดียว (อ่านกลับด้วย `unescape` ได้ตรงกับที่เขียน)

---

## เริ่มต้นใช้งานอย่างเร็ว

```cpp
#include "SQLog/sqlog.hpp"

int main() {
    // 1) เปิดโฟลเดอร์ log (ต้องมีอยู่แล้ว) — สร้าง init.json และ main.log ให้ถ้ายังไม่มี
    if (LoadLogs("test/logs") != SQLOG_SUCCESS) return 1;

    // 2) เขียน log
    logInfo ("network", "connected to server");
    logError("network", "timeout after 30s");
    append("AUDIT", "admin", "user \"bob\" deleted\nrecord 42");   // tag กำหนดเอง

    // 3) โหลดไฟล์ log ปัจจุบันเข้า memory แล้วค้นหา
    parse(getLogPath());
    doString(R"(search * where tag = "ERROR" order by time desc limit 20)");
    doString("count group by tag");
}
```

ผลลัพธ์ของ query พิมพ์เป็นตารางลง `stdout`:

```
id | tag   | time                | config  | content
---+-------+---------------------+---------+------------------
1  | ERROR | 2026-11-22T10:15:09 | network | timeout after 30s
(1 row)
```

---

## โปรแกรมตัวอย่าง (`main.cpp`) และ Command line

### ลำดับการทำงาน

```
main()
 ├─ หา MotherPath (root โปรเจกต์) จาก args[0]
 ├─ ReadConfig()        → อ่าน config/config.json เข้า MainConfig
 ├─ emitConfig()        → แปลง argument -d / -port / -mode / -name ไปทับค่าใน MainConfig
 │                        argument อื่น ๆ ที่ไม่รู้จักถูกส่งต่อเป็น vector<string>
 ├─ ถ้า debug = true    → พิมพ์ "[Debug] Debug Mode"
 └─ mainExecute(args)   → LoadLogs → loadProfile → parse → doString
```

### Argument ที่รองรับ

| Argument | ชนิด | key ใน config | ตัวอย่าง |
|---|---|---|---|
| `-d` | bool | `debug` | `-d` หรือ `-d=true` / `-d=1` / `-d=false` |
| `-port` | int | `port` | `-port=8080` |
| `-mode` | char | `mode` | `-mode=a` (เก็บเฉพาะตัวอักษรตัวแรก) |
| `-name` | string | `name` | `-name=server1` |

กฎ:

- รูปแบบคือ `<key>=<value>` ยกเว้นชนิด `bool` ที่ใส่แค่ `-d` ได้ (ค่า = `true`)
- ชนิดที่ไม่ใช่ bool ถ้าไม่ใส่ `=value` โปรแกรมจะแสดง `[Error]` แล้วออกด้วย exit code `1`
- argument ที่ไม่อยู่ในตารางจะไม่ถูกมองเป็น config แต่ส่งต่อให้ `mainExecute(args)`
- ค่าจาก command line **ทับ** ค่าใน `config.json`

```bash
./bin/run.out -d -port=8080 -name=server1 hello world
# debug=true, port=8080, name="server1", args = {"hello", "world"}
```

### เพิ่ม argument ใหม่

แก้ตาราง `emitConfigMap` ใน `main.cpp` — รูปแบบ `{"<flag>", {"<ชนิด>", "<key ใน config>"}}` โดยชนิดที่รองรับคือ `bool`, `int`, `char`, `string`

```cpp
map<string,vector<string>> emitConfigMap = {
    {"-d",    {"bool",   "debug"}},
    {"-port", {"int",    "port"}},
    {"-mode", {"char",   "mode"}},
    {"-name", {"string", "name"}},
    {"-log",  {"string", "logPath"}}   // เพิ่มใหม่
};
```

### `mainExecute` ที่ถูกต้อง

ฟังก์ชันใน SQLog เป็น **global function** ไม่ได้อยู่ใน namespace — `SQLOG_HPP` และ `SQLOG_ENGINE_HPP` เป็นแค่ **include guard macro** จึงเขียน `SQLOG_HPP::LoadLogs(...)` ไม่ได้ (จะ compile ไม่ผ่าน) ให้เรียกตรง ๆ แบบนี้:

```cpp
void mainExecute(vector<string> args) {
    if (LoadLogs(fs::path("./test/logs")) != SQLOG_SUCCESS) {
        throw runtime_error("cannot open ./test/logs");
    }
    loadProfile("main.log");
    parse(getLogPath());

    int r = doString("print \"Hello from SQLog\"");
    if (r != SQLOG_ENGINE_SUCCESS) {
        cerr << "[Error] " << SQLogLastError << endl;
    }
}
```

> หมายเหตุ: ใน `sqlog.hpp` กับ `engine.hpp` มี enum คนละตัว (`SQLogError` / `SQLogEngineError`) ที่ค่าตัวเลขเหมือนกันบางตัว ให้เทียบกับ enum ของฟังก์ชันที่เรียกเสมอ (`LoadLogs` → `SQLOG_*`, `parse`/`doString` → `SQLOG_ENGINE_*`)

### ตัวอย่าง: ทำเป็น shell โต้ตอบ (REPL)

`main.cpp` มีฟังก์ชัน `input()` ไว้แล้ว ใช้ทำ REPL ได้ทันที:

```cpp
void mainExecute(vector<string> args) {
    LoadLogs("./test/logs");
    parse(getLogPath());

    while (true) {
        string line = input("sqlog> ");
        if (cin.eof() || line == "exit" || line == "quit") break;
        if (line.empty()) continue;
        doString(line);          // error จะถูกพิมพ์ลง stderr โดย engine เอง
    }
}
```

```
sqlog> count group by tag
tag   | count
------+------
INFO  | 120
ERROR | 7
(2 rows)
sqlog> search * where tag = "ERROR" limit 2
...
```

---

## การตั้งค่า

### 1) `config/config.json` — config ของโปรแกรม

อ่านด้วย `ReadConfig()` เข้าตัวแปร `MainConfig` (`nlohmann::json`) และถูก override ด้วย argument ตอนรัน (ดูหัวข้อก่อนหน้า) เนื้อหาของไฟล์นี้กำหนดเองได้ตามที่โปรแกรมต้องการ เช่น

```json
{
    "debug": false,
    "port": 8080,
    "mode": "a",
    "name": "sqlog"
}
```

ถ้าไม่พบไฟล์หรือ JSON พัง `ReadConfig()` จะ throw และโปรแกรมแสดง `[Error] ...` ก่อนจบด้วย exit code `1`

### 2) `init.json` — config ของระบบ log

อยู่ในโฟลเดอร์ log (เช่น `test/logs/init.json`) สร้างให้อัตโนมัติถ้ายังไม่มี

```json
{
    "MainLogsFile": "main.log",
    "SaveLastLog": false,
    "LogSaveInit": {},
    "MaxLogSize": 4000
}
```

| คีย์ | ค่าเริ่มต้น | ความหมาย |
|---|---|---|
| `MainLogsFile` | `"main.log"` | ชื่อไฟล์ log หลัก |
| `SaveLastLog` | `false` | `true` = ตอนไฟล์เต็มย้ายไฟล์เก่าไป `archive/` เป็น `main_YYYYMMDD_HHMMSS.log`, `false` = ล้างไฟล์ทิ้ง |
| `MaxLogSize` | `4000` | ขนาดสูงสุดต่อไฟล์ หน่วย **KB** |
| `LogSaveInit` | `{}` | สำรองไว้ (ปัจจุบันยังไม่ถูกใช้) |

ถ้าอ่าน `init.json` ไม่ได้จะใช้ค่าเริ่มต้นและแจ้งเตือนทาง `stderr`

---

## API Reference

### `sqlog.hpp` — เขียน log

| ฟังก์ชัน | หน้าที่ |
|---|---|
| `int LoadLogs(const fs::path& folder)` | เปิดโฟลเดอร์ log, อ่าน `init.json`, เปิด/สร้างไฟล์ log หลัก |
| `int append(tag, config, content)` | เขียน 1 บรรทัด (thread-safe, หมุนไฟล์อัตโนมัติ) |
| `logInfo / logWarn / logError / logDebug(config, msg)` | shortcut ของ `append` ตาม tag |
| `int createNewLog(name)` | สร้างไฟล์ log ใหม่ (ล้างถ้ามีอยู่) แล้วสลับไปเขียนไฟล์นั้น |
| `int loadProfile(name)` | สลับไปเขียนไฟล์ log ที่มีอยู่แล้ว |
| `fs::path getLogPath()` | path เต็มของไฟล์ log ที่เปิดอยู่ |

คืนค่าเป็น `SQLogError`:
`SQLOG_SUCCESS`, `SQLOG_FILE_NOT_FOUND`, `SQLOG_FOLDER_NOT_FOUND`, `SQLOG_ERROR_TYPE`, `SQLOG_NOT_OPEN`

### `engine.hpp` — อ่าน/ค้นหา log

| ฟังก์ชัน / ตัวแปร | หน้าที่ |
|---|---|
| `int parse(fs::path file)` | อ่านไฟล์ log เข้า `LogBuffer` (แทนที่ของเดิมทั้งหมด) |
| `int doString(const string& query)` | รัน query หนึ่งหรือหลายคำสั่ง |
| `int clear()` | ล้าง `LogBuffer` |
| `vector<LogLine> LogBuffer` | log ทั้งหมดที่โหลดอยู่ |
| `vector<LogLine> SQLogResult` | ผลลัพธ์ล่าสุดของ `search` / `head` / `tail` |
| `string SQLogLastError` | ข้อความ error ล่าสุดจาก `doString` |

```cpp
struct LogLine {
    int    id;       // ลำดับในไฟล์ เริ่มที่ 0
    string content;
    string tag;
    string config;
    string time;
};
```

ใช้ผลลัพธ์ต่อใน C++:

```cpp
doString(R"(search * where tag = "ERROR" and config = "network")");
for (const LogLine& l : SQLogResult) {
    cout << l.time << " " << l.content << "\n";
}
```

---

## ภาษา Query

- keyword **ไม่สนตัวพิมพ์เล็ก/ใหญ่**
- `;` ท้ายคำสั่งใส่หรือไม่ใส่ก็ได้ และเขียนหลายคำสั่งต่อกันได้
- คอมเมนต์ใช้ `-- ข้อความ`
- ค่า (value) มี 2 แบบ: string `"..."` (escape ด้วย `\"`, `\\`, `\n`) และจำนวนเต็ม `123`

### คำสั่ง

| คำสั่ง | ไวยากรณ์ |
|---|---|
| ค้นหา | `search [distinct] [* \| field, ...] [where <cond>] [order by f [asc\|desc], ...] [limit N [offset M]]` |
| ค้นหา (alias) | `select ...` — เหมือน `search` |
| นับ | `count [where <cond>] [group by field]` |
| ลบ | `delete where <cond>` — ลบเฉพาะใน memory (ใช้ `save` เพื่อบันทึก) |
| แถวแรก | `head [N]` (ค่าเริ่มต้น 10) |
| แถวสุดท้าย | `tail [N]` (ค่าเริ่มต้น 10) |
| สรุป | `show tags \| configs \| stats` |
| โหลดไฟล์ | `load "file.log"` |
| บันทึกไฟล์ | `save "file.log"` — เขียน rows ใน memory ลงไฟล์ |
| ล้าง memory | `clear` |
| พิมพ์ข้อความ | `print "text", "text2", ...` |
| ช่วยเหลือ | `help` |

### Field

`id`, `tag`, `time`, `config`, `content`

- `id` เปรียบเทียบเป็น **ตัวเลข** (และต้องเทียบกับตัวเลขเท่านั้น)
- field อื่นเปรียบเทียบเป็น **string**

### เงื่อนไข (`where`)

| รูปแบบ | ความหมาย |
|---|---|
| `field = v`, `!=`, `<>`, `<`, `>`, `<=`, `>=` | เปรียบเทียบ |
| `field [not] like "net%"` | SQL LIKE: `%` = อะไรก็ได้, `_` = 1 ตัวอักษร, ไม่สนตัวพิมพ์ |
| `field [not] contains "timeout"` | มีข้อความนี้อยู่ข้างใน, ไม่สนตัวพิมพ์ |
| `field [not] regex "err(or)?\\d+"` | ECMAScript regex, ไม่สนตัวพิมพ์ (`matches` ใช้แทน `regex` ได้) |
| `field [not] in ("a", "b")` | อยู่ในรายการ |
| `field [not] between "x" and "y"` | อยู่ในช่วง (รวมขอบ) |
| `not <cond>`, `and`, `or`, `( ... )` | รวมเงื่อนไข (ลำดับความสำคัญ: `not` > `and` > `or`) |

---

## ตัวอย่าง Query

```sql
-- ERROR ล่าสุด 20 รายการ
search * where tag = "ERROR" order by time desc limit 20

-- timeout ที่ไม่ได้มาจาก config ที่ขึ้นต้นด้วย test
search tag, config where content contains "timeout" and not config like "test%"

-- config ที่เคยมี WARN หรือ ERROR (ไม่ซ้ำ)
search distinct config where tag in ("WARN", "ERROR")

-- log ของวันที่ 2026-11-22
search where time between "2026-11-22T00:00:00" and "2026-11-22T23:59:59"
search where time like "2026-11-22%" and id >= 100

-- แบ่งหน้า
search * order by id limit 50 offset 100

-- ใช้ regex
search content where content regex "(timeout|refused) after \\d+s"

-- นับตาม tag / สรุปภาพรวม
count group by tag
show stats

-- ลบ log DEBUG แล้วบันทึกเป็นไฟล์ใหม่
load "test/logs/main.log"
delete where tag = "DEBUG"
save "test/logs/clean.log"
```

---

## Error code

`doString`, `parse` ฯลฯ คืนค่าเป็น `SQLogEngineError`:

| ค่า | ความหมาย |
|---|---|
| `SQLOG_ENGINE_SUCCESS` | สำเร็จ |
| `SQLOG_ENGINE_FILE_NOT_FOUND` | ไม่พบไฟล์ |
| `SQLOG_ENGINE_FOLDER_NOT_FOUND` | ไม่พบโฟลเดอร์ |
| `SQLOG_ENGINE_ERROR_TYPE` | path เป็นโฟลเดอร์ (ต้องการไฟล์) |
| `SQLOG_ENGINE_NOT_OPEN` | ยังไม่ได้เปิด |
| `SQLOG_ENGINE_INVALID_GRAMMAR` | ไวยากรณ์ (PEG) ผิด |
| `SQLOG_ENGINE_INVALID_INPUT` | ไฟล์ไม่ตรงรูปแบบ log หรือ query มี syntax error |
| `SQLOG_ENGINE_ERROR_AST` | log บรรทัดไม่สมบูรณ์ (ไม่มี tag/time/config) |
| `SQLOG_ENGINE_RUNTIME_ERROR` | query ผิดตอนรัน เช่น regex พัง, `id` เทียบกับข้อความ, `load`/`save` ไม่สำเร็จ |

ข้อความ error ล่าสุดอยู่ใน `SQLogLastError` (และถูกพิมพ์ทาง `stderr` ด้วย)

---

## ข้อควรรู้ / ข้อจำกัด

- **ตัวแปร global**: `LogBuffer`, `SQLogResult`, `LogPath` ฯลฯ เป็น global (`inline`) — `append` ป้องกันด้วย mutex แต่ **engine (`parse`, `doString`) ไม่ thread-safe** ควรเรียกจาก thread เดียว
- `load` / `parse` รับ path ตามที่ให้มา (ไม่อิงกับ `LogPath`) และอ่านทั้งไฟล์เข้า memory — ถ้ามี **บรรทัดผิดรูปแบบแม้บรรทัดเดียว** จะโหลดไม่สำเร็จทั้งไฟล์ และ `LogBuffer` เดิมจะไม่ถูกแทนที่
- `id` คือลำดับบรรทัดตอนโหลด (เริ่ม 0) ไม่ถูกเก็บลงไฟล์ และ `delete` ไม่เลื่อน id ของแถวที่เหลือ
- `time` เปรียบเทียบแบบ string ซึ่งถูกต้องเพราะเป็น ISO-8601
- `head` / `tail` ไม่รับ `where` — ใช้ `search ... limit` แทน
- `group by` รับได้ 1 field และไม่มี aggregate function (`sum`, `avg` ฯลฯ)
- เวลาที่เขียน log เป็น local time ของเครื่อง
- ไฟล์อาจเกิน `MaxLogSize` ได้เล็กน้อย (ไม่เกิน 1 บรรทัด) เพราะตรวจขนาดก่อนเขียน
- `-mode` เก็บเป็น `char` ลง JSON — nlohmann/json จะเก็บเป็น **ตัวเลข (รหัส ASCII)** ไม่ใช่ string ถ้าต้องการเป็นตัวอักษรให้เปลี่ยนเป็น `MainConfig[key] = string(1, val[0]);`
- `main.cpp` ใช้ `fs::canonical(args[0])` — ถ้ารันผ่าน `PATH` (ไม่มี `/` ใน path) อาจหาไฟล์ไม่เจอ ให้รันด้วย path เช่น `./bin/run.out`

---

## Troubleshooting

| อาการ | สาเหตุ / วิธีแก้ |
|---|---|
| `'SQLOG_HPP' has not been declared as a class or namespace` | เรียกเป็น `SQLOG_HPP::LoadLogs` ให้เปลี่ยนเป็น `LoadLogs` (ดูหัวข้อ `mainExecute`) |
| `[Error] basic_ios::clear` / `parse error` ตอนเริ่ม | ไม่พบ `config/config.json` — รัน binary จาก `bin/` ของโปรเจกต์ และตรวจว่า config อยู่ที่ root |
| `syntax error at 1:N: ...` | query ผิดไวยากรณ์ — ลองพิมพ์ `help` ดูรูปแบบ |
| `file is not in [Tag] [time] [config] "content" format` | ไฟล์ log มีบรรทัดผิดรูปแบบ ตรวจ timestamp, วงเล็บ `[ ]` และเครื่องหมาย `"` |
| `id must be compared with a number` | ใช้ `id = "abc"` — `id` ต้องเทียบกับตัวเลข |
| `invalid regex: ...` | regex ผิดไวยากรณ์ ECMAScript — ใน query ต้อง escape `\` เป็น `\\` |
| `LoadLogs` คืน `SQLOG_FOLDER_NOT_FOUND` | ต้องสร้างโฟลเดอร์ log ก่อน (ไลบรารีไม่สร้างโฟลเดอร์ให้) |