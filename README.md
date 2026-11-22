# SQLog

ไลบรารี C++ แบบ header-only สำหรับ **เขียน log** และ **ค้นหา log ด้วยภาษา query สไตล์ SQL**

- `SQLog.hpp` — เขียน log ลงไฟล์ พร้อมระบบหมุนไฟล์ (rotate) เมื่อเต็ม
- `SQLogEngine.hpp` — โหลดไฟล์ log เข้า memory แล้วสั่ง `search`, `count`, `delete` ฯลฯ

```
[ERROR] [2026-11-22T10:05:12] [network] "connection \"timeout\" on port 80"
   │              │                │                    │
  Tag            time            config              content
```

```sql
search * where tag = "ERROR" and content contains "timeout" order by time desc limit 20
```

---

## ความต้องการ

| รายการ | รายละเอียด |
|---|---|
| คอมไพเลอร์ | รองรับ **C++20** (เช่น g++ 13+) |
| [nlohmann/json](https://github.com/nlohmann/json) | ใช้อ่าน `init.json` (include เป็น `nlohmann/json.hpp`) |
| [cpp-peglib](https://github.com/yhirose/cpp-peglib) | ใช้เฉพาะ `SQLogEngine.hpp` (include เป็น `peglib.h`) |
| Thread | คอมไพล์ด้วย `-pthread` |

## การติดตั้ง

คัดลอก `SQLog.hpp` และ `SQLogEngine.hpp` ไปไว้ในโฟลเดอร์ include ของโปรเจกต์ แล้ววาง `peglib.h` กับ `nlohmann/json.hpp` ไว้ใน include path เดียวกัน

```
src/
├── main.cpp
└── include/
    ├── SQLog.hpp
    ├── SQLogEngine.hpp
    ├── peglib.h
    └── nlohmann/
        └── json.hpp
```

คอมไพล์:

```bash
g++ ./src/*.cpp -I ./src/include -std=c++20 -pthread -o ./bin/run.out
```

---

## เริ่มต้นใช้งานอย่างเร็ว

```cpp
#include "SQLog.hpp"
#include "SQLogEngine.hpp"

int main() {
    // 1) เปิดโฟลเดอร์ log (ถ้าไม่มี init.json / main.log จะสร้างให้)
    LoadLogs("logs");

    // 2) เขียน log
    logInfo("startup", "program started");
    logError("network", "connection timeout");
    append("AUDIT", "login", "user admin logged in");

    // 3) ค้นหา
    doString("load \"logs/main.log\"");
    doString("search * where tag = \"ERROR\" order by time desc limit 10");
}
```

ผลลัพธ์:

```
id | tag   | time                | config  | content
---+-------+---------------------+---------+-------------------
1  | ERROR | 2026-11-22T10:05:12 | network | connection timeout
(1 row)
```

---

## 1. SQLog.hpp — การเขียน log

### รูปแบบบรรทัด

```
[Tag] [time] [config] "content"
```

| ส่วน | รูปแบบ | หมายเหตุ |
|---|---|---|
| `Tag` | ข้อความไม่มีช่องว่าง | เช่น `INFO`, `ERROR` |
| `time` | `YYYY-MM-DDTHH:MM:SS` | เวลาท้องถิ่น ใช้ `T` คั่น เพราะห้ามมีช่องว่าง |
| `config` | ข้อความไม่มีช่องว่าง | เช่น ชื่อโมดูล |
| `content` | ข้อความใดก็ได้ ครอบด้วย `"..."` | `"` `\` และขึ้นบรรทัดใหม่จะถูก escape ให้ ทำให้ 1 log = 1 บรรทัดเสมอ |

### ระบบกัน space (sanitize)

`tag`, `time` และ `config` ผ่านฟังก์ชัน `sanitizeField` ก่อนเขียนเสมอ

| อินพุต | ผลลัพธ์ |
|---|---|
| space / tab / ขึ้นบรรทัดใหม่ | เปลี่ยนเป็น `_` (ที่ติดกันยุบเหลือตัวเดียว, ตัดหัวท้าย) |
| `[` `]` | เปลี่ยนเป็น `(` `)` |
| ค่าว่าง | `-` |

```cpp
append("my tag", "net work [v2]", "hello");
// [my_tag] [2026-11-22T10:00:00] [net_work_(v2)] "hello"
```

### ไฟล์ตั้งค่า `init.json`

สร้างอัตโนมัติในโฟลเดอร์ log ถ้ายังไม่มี

```json
{
    "MainLogsFile": "main.log",
    "SaveLastLog": false,
    "LogSaveInit": {},
    "MaxLogSize": 4000
}
```

| คีย์ | ความหมาย |
|---|---|
| `MainLogsFile` | ชื่อไฟล์ log หลัก |
| `SaveLastLog` | `true` = เก็บไฟล์เก่าไว้ใน `archive/` เมื่อเต็ม, `false` = ล้างไฟล์ทิ้ง |
| `MaxLogSize` | ขนาดสูงสุดต่อไฟล์ หน่วย **KB** |
| `LogSaveInit` | สงวนไว้ใช้ในอนาคต (ยังไม่ถูกใช้งาน) |

### ระบบ log เต็ม (rotation)

ก่อนเขียนทุกครั้ง ระบบจะเช็กขนาดไฟล์เทียบกับ `MaxLogSize`

- `SaveLastLog = true` → ย้ายไฟล์เก่าไปที่ `archive/main_20261122_153000.log` แล้วเริ่มไฟล์ใหม่
- `SaveLastLog = false` → ล้างไฟล์แล้วเริ่มใหม่

```
logs/
├── init.json
├── main.log
└── archive/
    ├── main_20261122_101500.log
    └── main_20261122_153000.log
```

### API

| ฟังก์ชัน | หน้าที่ |
|---|---|
| `LoadLogs(path)` | เปิดโฟลเดอร์ log, อ่าน `init.json`, เปิดไฟล์หลัก |
| `append(tag, config, content)` | เขียน log 1 บรรทัด (thread-safe) |
| `logInfo / logWarn / logError / logDebug(config, msg)` | shortcut ของ `append` ที่ตั้ง tag ให้ |
| `createNewLog(name)` | สร้างไฟล์ log ใหม่แล้วใช้เป็นไฟล์ปัจจุบัน |
| `loadProfile(name)` | สลับไปใช้ไฟล์ log ที่มีอยู่แล้ว |

ทุกฟังก์ชันคืนค่า `SQLogError`:

| ค่า | ความหมาย |
|---|---|
| `FILE_NOT_FOUND` | ไม่พบไฟล์ |
| `FOLDER_NOT_FOUND` | ไม่พบโฟลเดอร์ |
| `ERROR_TYPE` | path เป็นผิดประเภท (เช่น ส่งไฟล์แทนโฟลเดอร์) |
| `NOT_OPEN` | ยังไม่ได้ `LoadLogs` หรือยังไม่ได้เปิดไฟล์ |
| `SUCCESS` | สำเร็จ |

> ใช้ `== SQLogError::SUCCESS` เช็กผลเสมอ อย่าเช็กว่าเป็น `0`

---

## 2. SQLogEngine.hpp — ภาษา query

เรียกผ่าน `doString("...")` ได้หลายคำสั่งในครั้งเดียว คั่นด้วย `;` (ไม่บังคับ)
keyword ไม่สนตัวพิมพ์เล็ก/ใหญ่ และเขียน comment แบบ `-- ข้อความ` ได้

### คำสั่ง

| คำสั่ง | ความหมาย |
|---|---|
| `search` / `select` | ค้นหา (สองคำนี้เหมือนกัน) |
| `count` | นับจำนวน |
| `delete where ...` | ลบออกจาก memory (ต้องมี `where` เสมอ) |
| `head [N]` / `tail [N]` | ดู N แถวแรก / สุดท้าย (ค่าเริ่มต้น 10) |
| `show tags` / `show configs` / `show stats` | สรุป tag, config และสถิติรวม |
| `load "path"` | โหลดไฟล์ log เข้า memory (แทนที่ของเดิม) |
| `save "path"` | เขียนข้อมูลใน memory ออกเป็นไฟล์ log |
| `clear` | ล้าง memory |
| `print "a", "b"` | พิมพ์ข้อความ |
| `help` | แสดงวิธีใช้ |

### `search`

```
search [distinct] [* | field, field, ...]
       [where <เงื่อนไข>]
       [order by field [asc|desc], ...]
       [limit N [offset M]]
```

Field ที่ใช้ได้: `id`, `tag`, `time`, `config`, `content`
ถ้าไม่ระบุ field จะแสดงทุกคอลัมน์

### เงื่อนไขใน `where`

| รูปแบบ | ตัวอย่าง |
|---|---|
| เปรียบเทียบ `=` `!=` `<>` `<` `>` `<=` `>=` | `tag = "ERROR"`, `id >= 100` |
| `like` (`%` = กี่ตัวก็ได้, `_` = 1 ตัว) | `config like "net%"` |
| `contains` | `content contains "timeout"` |
| `regex` (ECMAScript) | `content regex "err(or)?[0-9]+"` |
| `in (...)` | `tag in ("WARN", "ERROR")` |
| `between ... and ...` | `time between "2026-11-22T00:00:00" and "2026-11-22T23:59:59"` |
| ปฏิเสธ | `not like`, `not in`, `not contains`, `not between`, `not regex` |
| รวมเงื่อนไข | `and`, `or`, `not`, `( ... )` |

ลำดับความสำคัญ: `not` → `and` → `or` (ใช้วงเล็บเพื่อบังคับลำดับได้)

### ตัวอย่าง

```sql
-- เอา ERROR ล่าสุด 20 รายการ
search * where tag = "ERROR" order by time desc limit 20

-- เลือกเฉพาะ field และกรองหลายเงื่อนไข
search tag, config where content contains "timeout" and not config like "test%"

-- รายชื่อ config ที่ไม่ซ้ำ ซึ่งมี WARN หรือ ERROR
search distinct config where tag in ("WARN", "ERROR")

-- ช่วงเวลา
search where time like "2026-11-22%" and id >= 100

-- แบ่งหน้า (หน้าที่ 2 หน้าละ 50)
search * order by id limit 50 offset 50

-- นับ
count
count where tag = "ERROR"
count group by tag

-- ลบแล้วบันทึก
delete where tag = "DEBUG"; save "logs/clean.log"
```

### ข้อควรรู้

- `=`, `!=`, `<`, `>` เทียบแบบ **แยกตัวพิมพ์เล็ก/ใหญ่** ส่วน `like`, `contains`, `regex` **ไม่แยก**
- `id` เทียบเป็นตัวเลข ส่วน field อื่นเทียบเป็นข้อความ
- `time` เทียบเป็นข้อความได้เพราะเป็นรูปแบบ ISO ถ้าต้องการทั้งวันให้ใช้ `time like "2026-11-22%"` เพราะ `time <= "2026-11-22"` จะไม่รวมวันที่ 22
- `delete` มีผลใน memory เท่านั้น ไฟล์จริงไม่เปลี่ยนจนกว่าจะ `save`
- `id` คือลำดับบรรทัดตอน `load` (เริ่มจาก 0) และจะไม่ถูกจัดเรียงใหม่หลัง `delete`

### ใช้ผลลัพธ์ใน C++

```cpp
int rc = doString("search * where tag = \"ERROR\" limit 10");
if (rc != SQLOG_ENGINE_SUCCESS) {
    std::cerr << SQLogLastError << "\n";
}

for (const LogLine& l : SQLogResult) {   // ผลลัพธ์ของ search / head / tail ล่าสุด
    std::cout << l.time << " " << l.content << "\n";
}
```

| ตัวแปร / ฟังก์ชัน | ความหมาย |
|---|---|
| `doString(code)` | รันคำสั่ง คืนค่า `SQLogEngineError` |
| `parse(path)` | โหลดไฟล์ log เข้า `LogBuffer` (เหมือนคำสั่ง `load`) |
| `LogBuffer` | log ทั้งหมดที่อยู่ใน memory (`vector<LogLine>`) |
| `SQLogResult` | แถวผลลัพธ์ล่าสุด |
| `SQLogLastError` | ข้อความ error ล่าสุด |

โครงสร้าง `LogLine`:

```cpp
struct LogLine { int id; string tag; string time; string config; string content; };
```

### รหัส error ของ engine (`SQLogEngineError`)

| ค่า | ความหมาย |
|---|---|
| `SQLOG_ENGINE_SUCCESS` | สำเร็จ |
| `SQLOG_ENGINE_FILE_NOT_FOUND` / `FOLDER_NOT_FOUND` / `ERROR_TYPE` | ปัญหาเรื่อง path |
| `SQLOG_ENGINE_INVALID_INPUT` | ไวยากรณ์ query ผิด (หรือไฟล์ log ไม่ตรงรูปแบบ) พร้อมบอกบรรทัด:คอลัมน์ |
| `SQLOG_ENGINE_ERROR_AST` | บรรทัด log ขาด tag / time / config |
| `SQLOG_ENGINE_RUNTIME_ERROR` | ผิดตอนรัน เช่น regex ไม่ถูกต้อง, เทียบ `id` กับข้อความ |
| `SQLOG_ENGINE_INVALID_GRAMMAR` | grammar ภายในพัง (ไม่ควรเกิด) |

> หมายเหตุ: ตาม enum ปัจจุบัน `SQLOG_ENGINE_SUCCESS` มีค่าเท่ากับ `4` ไม่ใช่ `0` ให้เช็กด้วยชื่อ

---

## ข้อจำกัด

- โหลดไฟล์ log ทั้งไฟล์เข้า memory ถ้าไฟล์ใหญ่มากอาจใช้ RAM เยอะ
- ใช้ค่า global ร่วมกัน (เปิดได้ครั้งละ 1 โฟลเดอร์ log)
- engine ค้นหาจากไฟล์ที่ `load` มาแล้ว ไม่ติดตามไฟล์สด ถ้ามี log ใหม่ต้อง `load` ใหม่
- ความกว้างตารางคำนวณตามจำนวนตัวอักษร ภาษาไทยที่มีสระ/วรรณยุกต์ลอยอาจเยื้องเล็กน้อย

## แผนในอนาคต

- `update`, ส่งออกเป็น JSON / CSV
- กรอง log level, เขียนลง console พร้อมกัน
- เขียนแบบ async

## License
เป็น Open Source สามารถนำไปใช้ได้ ตัว เครื่องมืออยู่ที่ ./src/include/SQLog